#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const headerPath = path.join(
  __dirname,
  '..',
  'components',
  'sesame_web',
  'include',
  'sesame_web',
  'captive_portal_html.h',
);
const header = fs.readFileSync(headerPath, 'utf8');
const webControlServerPath = path.join(
  __dirname,
  '..',
  'components',
  'sesame_web',
  'web_control_server.cpp',
);
const webControlServer = fs.readFileSync(webControlServerPath, 'utf8');
assert.match(header, /id="actionControls"/,
  'the page must render the complete action library from the firmware catalog');
assert.match(header, /id="faceControls"/,
  'the page must render the complete OLED-face library from the firmware catalog');
assert.ok(webControlServer.includes('.uri = "/api/catalog"'),
  'the web server must expose the action and face catalog');
assert.match(header, /id="controlStatus"/, 'web page must include a visible control-status region');
assert.match(
  header,
  /data-direction="forward"/,
  'direction controls must expose an explicit pointer target instead of mixing mouse and touch attributes',
);
const match = header.match(/<script>([\s\S]*?)<\/script>/);
if (!match) throw new Error('embedded web control script was not found');

const elements = new Map();
function createElement(dataset = {}) {
  const classNames = new Set();
  const listeners = new Map();
  return {
    textContent: '',
    className: '',
    value: '',
    disabled: false,
    style: {},
    dataset,
    setAttribute() {},
    setPointerCapture() {},
    releasePointerCapture() {},
    classList: {
      add(name) { classNames.add(name); },
      remove(name) { classNames.delete(name); },
      toggle(name, enabled) {
        if (enabled) classNames.add(name);
        else classNames.delete(name);
      },
      contains(name) { return classNames.has(name); },
    },
    addEventListener(type, listener) { listeners.set(type, listener); },
    dispatch(type, event = {}) {
      const listener = listeners.get(type);
      if (listener) {
        listener({
          currentTarget: this,
          preventDefault() {},
          ...event,
        });
      }
    },
  };
}

const directionalButtons = [
  createElement({ direction: 'forward' }),
  createElement({ direction: 'backward' }),
  createElement({ direction: 'left' }),
  createElement({ direction: 'right' }),
];
const documentListeners = new Map();
const document = {
  documentElement: { style: { setProperty() {} } },
  addEventListener(type, listener) { documentListeners.set(type, listener); },
  getElementById(id) {
    if (!elements.has(id)) elements.set(id, createElement());
    return elements.get(id);
  },
  querySelectorAll(selector) {
    if (selector === '[data-direction]') return directionalButtons;
    return [];
  },
};

const windowListeners = new Map();
const window = {
  addEventListener(type, listener) { windowListeners.set(type, listener); },
  dispatch(type, event = {}) {
    const listener = windowListeners.get(type);
    if (listener) listener(event);
  },
};

let fetchImplementation = async () => ({
  ok: false,
  json: async () => ({ ok: false, message: 'invalid servo command' }),
});
const context = {
  console: { log() {} },
  document,
  window,
  navigator: {},
  localStorage: { getItem() { return null; }, setItem() {} },
  setTimeout() { return 0; },
  clearInterval() {},
  setInterval() { return 0; },
  fetch(...args) { return fetchImplementation(...args); },
};
vm.createContext(context);
vm.runInContext(match[1], context);

async function settle() {
  await new Promise((resolve) => setImmediate(resolve));
  await new Promise((resolve) => setImmediate(resolve));
}

async function run() {
  context.updateMotor(1, '95');
  await settle();
  const status = document.getElementById('controlStatus');
  assert.match(
    status.textContent,
    /invalid servo command/i,
    'a rejected manual-servo request must be visible in the web control page',
  );
  assert.match(
    status.className,
    /error/,
    'a rejected manual-servo request must be styled as an error',
  );

  const requests = [];
  let resolveForward;
  fetchImplementation = (url) => {
    requests.push(url);
    if (url === '/cmd?go=forward') {
      return new Promise((resolve) => { resolveForward = resolve; });
    }
    return Promise.resolve({ ok: true, json: async () => ({ ok: true, message: 'ok' }) });
  };

  const forward = directionalButtons.find((button) => button.dataset.direction === 'forward');
  forward.dispatch('pointerdown', { pointerId: 1 });
  forward.dispatch('pointerdown', { pointerId: 1 });
  await settle();
  assert.deepEqual(requests, ['/cmd?go=forward'], 'one held pointer must send one Go request');

  forward.dispatch('pointerup', { pointerId: 1 });
  await settle();
  assert.deepEqual(
    requests,
    ['/cmd?go=forward'],
    'Stop must wait for the matching Go request so a quick click cannot reverse request order',
  );

  resolveForward({ ok: true, json: async () => ({ ok: true, message: 'movement started' }) });
  await settle();
  assert.deepEqual(requests, ['/cmd?go=forward', '/cmd?stop=1']);

  let resolveLeft;
  fetchImplementation = (url) => {
    requests.push(url);
    if (url === '/cmd?go=left') {
      return new Promise((resolve) => { resolveLeft = resolve; });
    }
    return Promise.resolve({ ok: true, json: async () => ({ ok: true, message: 'ok' }) });
  };
  const left = directionalButtons.find((button) => button.dataset.direction === 'left');
  left.dispatch('pointerdown', { pointerId: 2 });
  await settle();
  left.dispatch('pointercancel', { pointerId: 2 });
  await settle();
  assert.equal(requests.at(-1), '/cmd?go=left', 'pointer cancellation must also wait for Go');
  resolveLeft({ ok: true, json: async () => ({ ok: true, message: 'movement started' }) });
  await settle();
  assert.equal(requests.at(-1), '/cmd?stop=1', 'pointer cancellation must safely stop movement');

  fetchImplementation = (url) => {
    requests.push(url);
    return Promise.resolve({ ok: true, json: async () => ({ ok: true, message: 'ok' }) });
  };
  const right = directionalButtons.find((button) => button.dataset.direction === 'right');
  right.dispatch('pointerdown', { pointerId: 3 });
  await settle();
  window.dispatch('blur');
  await settle();
  assert.equal(requests.at(-1), '/cmd?stop=1', 'losing window focus must safely stop movement');

  const switchStart = requests.length;
  let resolveBackward;
  fetchImplementation = (url) => {
    requests.push(url);
    if (url === '/cmd?go=backward') {
      return new Promise((resolve) => { resolveBackward = resolve; });
    }
    return Promise.resolve({ ok: true, json: async () => ({ ok: true, message: 'ok' }) });
  };
  const backward = directionalButtons.find((button) => button.dataset.direction === 'backward');
  backward.dispatch('pointerdown', { pointerId: 4 });
  await settle();
  backward.dispatch('pointerup', { pointerId: 4 });
  left.dispatch('pointerdown', { pointerId: 5 });
  left.dispatch('pointerup', { pointerId: 5 });
  resolveBackward({ ok: true, json: async () => ({ ok: true, message: 'movement started' }) });
  await settle();
  assert.equal(
    requests.slice(switchStart).includes('/cmd?go=left'),
    false,
    'a released replacement direction must not start after the prior Go/Stop pair finishes',
  );
}

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});

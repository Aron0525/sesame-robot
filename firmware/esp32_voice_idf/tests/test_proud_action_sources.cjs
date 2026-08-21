#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const projectRoot = path.join(__dirname, '..', '..', '..');
const idfSequence = path.join(
  projectRoot,
  'firmware',
  'esp32_voice_idf',
  'components',
  'sesame_web',
  'include',
  'sesame_web',
  'legacy_movement_sequences.h',
);
const arduinoSequence = path.join(
  projectRoot,
  'firmware',
  'arduino',
  'Sesame_Robot_WiFi_Controller',
  'movement-sequences.h',
);

const expectedFrames = [
  [135, 45, 45, 135, 23, 128, 26, 127],
  [135, 45, 45, 135, 53, 98, 56, 97],
  [135, 45, 45, 135, 23, 128, 26, 127],
];

function proudFrames(filePath) {
  const source = fs.readFileSync(filePath, 'utf8');
  const match = source.match(/inline void runProudPose\(\) \{([\s\S]*?)\n\}/);
  assert.ok(match, `${filePath}: runProudPose must exist`);
  const body = match[1];
  const calls = [...body.matchAll(/setServoAngle\((R1|R2|L1|L2|R4|R3|L3|L4),\s*(\d+)\)/g)]
    .map((call) => Number(call[2]));
  assert.equal(calls.length, 24, `${filePath}: proud must define three complete 8-servo keyframes`);
  assert.deepEqual(
    [calls.slice(0, 8), calls.slice(8, 16), calls.slice(16, 24)],
    expectedFrames,
    `${filePath}: proud keyframes must match the requested standing-angle offsets`,
  );
  assert.match(body, /for \(int i = 0; i < 5; i\+\+\)/,
    `${filePath}: proud must repeat B to A five times`);
  assert.equal((body.match(/delayWithFace\(300\)/g) || []).length, 3,
    `${filePath}: every A/B transition must wait 300 ms`);
  assert.match(body, /runStandPose\(1\)/,
    `${filePath}: proud must return to stand`);
}

proudFrames(idfSequence);
proudFrames(arduinoSequence);

const catalog = fs.readFileSync(path.join(
  projectRoot,
  'firmware',
  'esp32_voice_idf',
  'components',
  'sesame_robot',
  'include',
  'sesame_robot',
  'control_catalog.h',
), 'utf8');
assert.match(catalog, /"proud"/, 'the IDF web action catalog must expose proud');

const idfPortal = fs.readFileSync(path.join(
  projectRoot,
  'firmware',
  'esp32_voice_idf',
  'components',
  'sesame_web',
  'include',
  'sesame_web',
  'captive_portal_html.h',
), 'utf8');
assert.match(idfPortal, /proud:\s*'得意'/,
  'the IDF web controller must display proud as 得意');

const runner = fs.readFileSync(path.join(
  projectRoot,
  'firmware',
  'esp32_voice_idf',
  'components',
  'sesame_web',
  'legacy_motion_runner.cpp',
), 'utf8');
assert.match(runner, /action == "proud"\)\s*\{?\s*runProudPose\(\)/,
  'the IDF motion runner must dispatch proud');

const arduinoController = fs.readFileSync(path.join(
  projectRoot,
  'firmware',
  'arduino',
  'Sesame_Robot_WiFi_Controller',
  'Sesame_Robot_WiFi_Controller.ino',
), 'utf8');
assert.match(arduinoController, /action == "proud"/,
  'the Arduino controller must accept proud');
assert.match(arduinoController, /cmd == "proud"\)\s*runProudPose\(\)/,
  'the Arduino controller must dispatch proud');

const arduinoPortal = fs.readFileSync(path.join(
  projectRoot,
  'firmware',
  'arduino',
  'Sesame_Robot_WiFi_Controller',
  'captive-portal.h',
), 'utf8');
assert.match(arduinoPortal, /pose\('proud'\).*?>得意</,
  'the Arduino web controller must show the 得意 action button');

console.log('PASS: proud action poses and web-control exposure are synchronized');

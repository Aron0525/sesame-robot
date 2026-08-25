#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const idfMovement = path.join(
  root,
  'components/sesame_web/include/sesame_web/legacy_movement_sequences.h',
);
const arduinoRoot = path.join(root, '../arduino/Sesame_Robot_WiFi_Controller');
const arduinoMovement = path.join(arduinoRoot, 'movement-sequences.h');

function body(source, action, nextAction) {
  const start = source.indexOf(`inline void ${action}()`);
  const end = source.indexOf(`inline void ${nextAction}()`, start);
  assert.ok(start >= 0 && end > start, `${action} body was not found`);
  return source.slice(start, end);
}

function writes(source) {
  return [...source.matchAll(/setServoAngle\((R[1-4]|L[1-4]),\s*(\d+)\)/g)]
    .map(([, servo, angle]) => `${servo}=${angle}`);
}

function delays(source) {
  return [...source.matchAll(/delayWithFace\((\d+)\)/g)]
    .map(([, milliseconds]) => Number(milliseconds));
}

const a = ['R1=135', 'R2=45', 'L1=45', 'L2=135',
  'R4=23', 'R3=128', 'L3=26', 'L4=127'];
const b = ['R1=135', 'R2=45', 'L1=45', 'L2=135',
  'R4=53', 'R3=98', 'L3=56', 'L4=97'];

for (const movementPath of [idfMovement, arduinoMovement]) {
  const proud = body(fs.readFileSync(movementPath, 'utf8'), 'runProudPose', 'runSwimPose');
  const loopStart = proud.indexOf('for (int i = 0; i < 5; i++) {');
  const loopEnd = proud.indexOf('  runStandPose(1);', loopStart);
  assert.ok(loopStart >= 0 && loopEnd > loopStart, 'proud loop was not found');

  const initial = proud.slice(0, loopStart);
  const loop = proud.slice(loopStart, loopEnd);
  assert.deepEqual(writes(initial), a, 'proud must begin in A');
  assert.deepEqual(delays(initial), [240], 'initial A must hold for 240 ms');
  assert.deepEqual(writes(loop), [...b, ...a], 'each iteration must be B then A');
  assert.deepEqual(delays(loop), [240, 240], 'B and A must each hold for 240 ms');
  assert.match(proud, /const int originalMotorCurrentDelay = motorCurrentDelay;/,
    'proud must preserve the configured servo-write interval');
  assert.match(proud, /motorCurrentDelay = \(originalMotorCurrentDelay \* 4\) \/ 5;/,
    'proud must use four-fifths of the configured servo-write interval');
  assert.match(proud, /motorCurrentDelay = originalMotorCurrentDelay;\s*runStandPose\(1\);/,
    'proud must restore the configured servo-write interval before standing');
  assert.match(proud.slice(loopEnd), /runStandPose\(1\)/,
    'proud must return to standing after five B-to-A iterations');
}

// Eight staggered writes (16 ms each) plus the 240 ms hold define one frame.
assert.equal(8 * 16 + 240, 368, 'proud keyframes must begin 368 ms apart');

const catalog = fs.readFileSync(path.join(
  root,
  'components/sesame_robot/include/sesame_robot/control_catalog.h',
), 'utf8');
assert.match(catalog, /"proud"/, 'IDF web action catalog must expose proud');

const runner = fs.readFileSync(path.join(
  root,
  'components/sesame_web/legacy_motion_runner.cpp',
), 'utf8');
assert.match(runner, /action == "proud"\)\s*\{\s*runProudPose\(\);/,
  'IDF action dispatcher must run proud');

const idfPortal = fs.readFileSync(path.join(
  root,
  'components/sesame_web/include/sesame_web/captive_portal_html.h',
), 'utf8');
assert.match(idfPortal, /proud:\s*'得意'/, 'IDF web page must label proud as 得意');

const arduinoPortal = fs.readFileSync(path.join(arduinoRoot, 'captive-portal.h'), 'utf8');
assert.match(arduinoPortal, /onclick="pose\('proud'\)"[^>]*>得意</,
  'Arduino web page must provide the 得意 button');

const arduinoSketch = fs.readFileSync(
  path.join(arduinoRoot, 'Sesame_Robot_WiFi_Controller.ino'),
  'utf8',
);
assert.match(arduinoSketch, /cmd == "proud"\) runProudPose\(\);/,
  'Arduino dispatcher must run proud');
assert.match(arduinoSketch, /"rn pr"\) == 0\) \{ currentCommand = "proud"; runProudPose\(\); \}/,
  'Arduino serial command rn pr must run proud');

console.log('PASS: proud implements the confirmed frames, timing, and controls');

#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const firmwareRoot = path.join(__dirname, '..');
const sources = [
  path.join(firmwareRoot, 'components', 'sesame_web', 'include', 'sesame_web',
    'legacy_movement_sequences.h'),
  path.join(firmwareRoot, '..', 'arduino', 'Sesame_Robot_WiFi_Controller',
    'movement-sequences.h'),
];

function body(source, action, nextAction) {
  const start = source.indexOf(`inline void ${action}()`);
  const end = source.indexOf(`inline void ${nextAction}()`, start);
  assert.ok(start >= 0 && end > start, `${action} must exist before ${nextAction}`);
  return source.slice(start, end);
}

function angles(actionBody, servo) {
  return [...actionBody.matchAll(new RegExp(`setServoAngle\\(${servo},\\s*(\\d+)\\)`, 'g'))]
    .map(([, angle]) => Number(angle));
}

for (const sourcePath of sources) {
  const source = fs.readFileSync(sourcePath, 'utf8');

  assert.match(source, /setServoAngle\(R4,\s*38\)/);
  assert.match(source, /setServoAngle\(R3,\s*113\)/);
  assert.match(source, /setServoAngle\(L3,\s*41\)/);
  assert.match(source, /setServoAngle\(L4,\s*112\)/);
  assert.match(source, /inline void runProudPose\(\)/);
  assert.doesNotMatch(source, /inline void runPointPose\(\)/);
  assert.doesNotMatch(source, /inline void runBowPose\(\)/);

  const dance = body(source, 'runDancePose', 'runProudPose');
  assert.deepEqual(angles(dance, 'R4'), [145, 130, 145]);
  assert.deepEqual(angles(dance, 'R3'), [145, 100, 145]);
  assert.deepEqual(angles(dance, 'L3'), [25, 25, 80]);
  assert.deepEqual(angles(dance, 'L4'), [25, 25, 80]);
  assert.equal(angles(dance, 'R1').length, 3,
    'dance must rewrite R1 in every keyframe');

  const swim = body(source, 'runSwimPose', 'runPushupPose');
  assert.deepEqual(angles(swim, 'R3'), [60]);
  assert.deepEqual(angles(swim, 'L4'), [60]);

  const pushup = body(source, 'runPushupPose', 'runCutePose');
  assert.deepEqual(angles(pushup, 'R3'), [60, 150, 60]);

  const worm = body(source, 'runWormPose', 'runShakePose');
  assert.deepEqual(angles(worm, 'R3'), [60, 15, 105]);
  assert.deepEqual(angles(worm, 'L4'), [60, 105, 15]);

  const shake = body(source, 'runShakePose', 'runShrugPose');
  assert.deepEqual(angles(shake, 'R3'), [60]);
  assert.deepEqual(angles(shake, 'L4'), [105, 150]);

  const crab = body(source, 'runCrabPose', 'runWalkPose');
  assert.deepEqual(angles(crab, 'R3'), [150, 105, 150]);
  assert.deepEqual(angles(crab, 'L4'), [105, 150, 105]);

  const forward = body(source, 'runWalkPose', 'runWalkBackward');
  assert.deepEqual(angles(forward, 'R3'), [98, 98, 143, 98]);
  assert.deepEqual(angles(forward, 'L3'), [56, 11, 56]);
  assert.deepEqual(angles(forward, 'R4'), [8, 53]);
  assert.deepEqual(angles(forward, 'L4'), [97, 142]);
  assert.match(forward, /pressingCheck\("forward", 80\)/);
  assert.match(forward, /pressingCheck\("forward", 20\)/);
}

const catalog = fs.readFileSync(path.join(firmwareRoot, 'components', 'sesame_robot',
  'include', 'sesame_robot', 'control_catalog.h'), 'utf8');
const actionCatalogStart = catalog.indexOf('inline constexpr std::array<std::string_view, 18> kWebActions');
const actionCatalog = catalog.slice(actionCatalogStart, catalog.indexOf('kWebExpressions'));
assert.match(actionCatalog, /std::array<std::string_view, 18>/);
assert.match(actionCatalog, /"proud"/);
assert.doesNotMatch(actionCatalog, /"point"/);
assert.doesNotMatch(actionCatalog, /"bow"/);

console.log('PASS: motion sources match the 0821 calibrated action set');

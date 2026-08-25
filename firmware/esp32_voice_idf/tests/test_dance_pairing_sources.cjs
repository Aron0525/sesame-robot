#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const movementSources = [
  path.join(root, 'components/sesame_web/include/sesame_web/legacy_movement_sequences.h'),
  path.join(root, '../arduino/Sesame_Robot_WiFi_Controller/movement-sequences.h'),
];

function danceBody(source) {
  const start = source.indexOf('inline void runDancePose()');
  const end = source.indexOf('inline void runProudPose()', start);
  assert.ok(start >= 0 && end > start, 'dance body was not found');
  return source.slice(start, end);
}

function angles(body, servo) {
  return [...body.matchAll(new RegExp(`setServoAngle\\(${servo},\\s*(\\d+)\\)`, 'g'))]
    .map(([, angle]) => Number(angle));
}

for (const sourcePath of movementSources) {
  const body = danceBody(fs.readFileSync(sourcePath, 'utf8'));
  const r4 = angles(body, 'R4');
  const l4 = angles(body, 'L4');
  const r3 = angles(body, 'R3');
  const l3 = angles(body, 'L3');

  assert.deepEqual(r4, [145, 90, 145],
    `${path.basename(sourcePath)}: R4 must drop then return during the dance`);
  assert.deepEqual(r3, [145, 55, 145],
    `${path.basename(sourcePath)}: R3 must use the specified 145° → 55° → 145° keyframes`);
  assert.deepEqual(l3, l4,
    `${path.basename(sourcePath)}: L3 must copy every L4 dance keyframe`);
  assert.equal(r3[0], r4[0],
    `${path.basename(sourcePath)}: R3 and R4 must begin parallel`);
  assert.ok(r3[1] < r4[1],
    `${path.basename(sourcePath)}: R3 must move below R4 at the middle dance keyframe`);
  assert.equal(r3[2], r4[2],
    `${path.basename(sourcePath)}: R3 and R4 must finish parallel`);
  assert.equal(Math.max(...l3) - Math.min(...l3), Math.max(...l4) - Math.min(...l4),
    `${path.basename(sourcePath)}: L3 and L4 must have the same dance amplitude`);
  assert.equal(Math.max(...r4) - Math.min(...r4), Math.max(...l4) - Math.min(...l4),
    `${path.basename(sourcePath)}: both dance arm pairs must use the same amplitude`);
}

console.log('PASS: dance has the requested R3 range and parallel L3/L4 motion');

#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const projectRoot = path.join(__dirname, '..');
const sources = [
  'components/sesame_web/include/sesame_web/legacy_movement_sequences.h',
  '../arduino/Sesame_Robot_WiFi_Controller/movement-sequences.h',
];

const expectedFrames = [
  [['R1', 90], ['R2', 90], ['L1', 90], ['L2', 90],
   ['R4', 145], ['R3', 145], ['L3', 25], ['L4', 25]],
  [['R1', 90], ['R2', 90], ['L1', 90], ['L2', 90],
   ['R4', 130], ['R3', 100], ['L3', 25], ['L4', 25]],
  [['R1', 90], ['R2', 90], ['L1', 90], ['L2', 90],
   ['R4', 145], ['R3', 145], ['L3', 80], ['L4', 80]],
];

function danceFrames(filePath) {
  const source = fs.readFileSync(filePath, 'utf8');
  const start = source.indexOf('inline void runDancePose() {');
  const end = source.indexOf('inline void runProudPose()', start);
  assert.ok(start >= 0 && end > start, `runDancePose body was not found in ${filePath}`);
  const calls = [...source.slice(start, end).matchAll(
    /setServoAngle\((R[1-4]|L[1-4]),\s*(\d+)\)/g,
  )].map(([, channel, angle]) => [channel, Number(angle)]);
  assert.equal(calls.length, 24, `${filePath}: dance must define three complete 8-servo frames`);
  return [calls.slice(0, 8), calls.slice(8, 16), calls.slice(16, 24)];
}

for (const relativePath of sources) {
  assert.deepEqual(
    danceFrames(path.join(projectRoot, relativePath)),
    expectedFrames,
    `${relativePath}: every dance keyframe must command all eight servos`,
  );
}

console.log('PASS: dance keyframes define complete eight-servo poses');

#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const projectRoot = path.join(__dirname, '..');
const targetCalls = [
  ['R1', 135], ['R2', 45], ['L1', 45], ['L2', 135],
  ['R4', 30], ['R3', 120], ['L3', 30], ['L4', 120],
];

function standPoseCalls(filePath) {
  const source = fs.readFileSync(filePath, 'utf8');
  const start = source.indexOf('inline void runStandPose(int face) {');
  const end = source.indexOf('inline void runWavePose', start);
  assert.ok(start >= 0 && end > start, `runStandPose body was not found in ${filePath}`);
  return [...source.slice(start, end).matchAll(/setServoAngle\((R[1-4]|L[1-4]),\s*(\d+)\)/g)]
    .map(([, channel, angle]) => [channel, Number(angle)]);
}

for (const relativePath of [
  'components/sesame_web/include/sesame_web/legacy_movement_sequences.h',
  '../arduino/Sesame_Robot_WiFi_Controller/movement-sequences.h',
]) {
  assert.deepEqual(
    standPoseCalls(path.join(projectRoot, relativePath)),
    targetCalls,
    `stand pose must match the target in ${relativePath}`,
  );
}

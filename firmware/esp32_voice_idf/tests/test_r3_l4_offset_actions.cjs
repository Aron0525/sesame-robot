#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const projectRoot = path.join(__dirname, '..');
const sources = [
  'components/sesame_web/include/sesame_web/legacy_movement_sequences.h',
  '../arduino/Sesame_Robot_WiFi_Controller/movement-sequences.h',
];

function functionBody(source, functionName, nextFunctionName) {
  const start = source.indexOf(`inline void ${functionName}`);
  const end = source.indexOf(`inline void ${nextFunctionName}`, start);
  assert.ok(start >= 0 && end > start, `${functionName} body was not found`);
  return source.slice(start, end);
}

function directAngle(body, channel) {
  const matches = [...body.matchAll(
    new RegExp(`setServoAngle\\(${channel},\\s*(\\d+)\\)`, 'g'),
  )];
  assert.equal(matches.length, 1, `${channel} must be set exactly once directly`);
  return Number(matches[0][1]);
}

for (const relativePath of sources) {
  const source = fs.readFileSync(path.join(projectRoot, relativePath), 'utf8');

  const rest = functionBody(source, 'runRestPose()', 'runStandPose');
  assert.match(rest, /setServoAngle\(R3,\s*60\)/,
               `${relativePath}: rest R3 must be 90 - 30 = 60`);
  assert.match(rest, /setServoAngle\(L4,\s*60\)/,
               `${relativePath}: rest L4 must be 90 - 30 = 60`);

  const stand = functionBody(source, 'runStandPose', 'runWavePose');
  assert.equal(directAngle(stand, 'R3'), 113,
               `${relativePath}: stand R3 matches the 0821 calibration`);
  assert.equal(directAngle(stand, 'L4'), 112,
               `${relativePath}: stand L4 matches the 0821 calibration`);

  const dead = functionBody(source, 'runDeadPose()', 'runCrabPose');
  assert.equal(directAngle(dead, 'R3'), 60,
               `${relativePath}: dead R3 must be 90 - 30 = 60`);
  assert.equal(directAngle(dead, 'L4'), 60,
               `${relativePath}: dead L4 must be 90 - 30 = 60`);
}

console.log('PASS: rest, stand, and dead apply the R3/L4 30-degree reduction');

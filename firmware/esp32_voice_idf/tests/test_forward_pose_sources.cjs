#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const projectRoot = path.join(__dirname, '..');
const movementPaths = [
  path.join(
    projectRoot,
    'components/sesame_web/include/sesame_web/legacy_movement_sequences.h',
  ),
  path.join(
    projectRoot,
    '../arduino/Sesame_Robot_WiFi_Controller/movement-sequences.h',
  ),
];

function functionBody(source, name, nextName) {
  const start = source.indexOf(`inline void ${name}()`);
  const end = nextName
    ? source.indexOf(`inline void ${nextName}()`, start)
    : source.length;
  assert.ok(start >= 0 && end > start, `${name} body was not found`);
  return source.slice(start, end);
}

function servoAngles(body, servo) {
  const pattern = new RegExp(`setServoAngle\\(${servo},\\s*(\\d+)\\)`, 'g');
  return [...body.matchAll(pattern)].map(([, angle]) => Number(angle));
}

const expected = {
  runWalkPose: {
    R1: [135, 90], R2: [90, 45, 90], L1: [45, 90, 45], L2: [90, 135],
    R3: [98, 98, 143, 98], R4: [8, 53], L3: [56, 11, 56], L4: [97, 142],
  },
  runWalkBackward: {
    R1: [90, 135], R2: [90, 45], L1: [45, 90], L2: [135, 90],
    R3: [98, 143], R4: [8, 53], L3: [11, 56], L4: [97, 142],
  },
  runTurnLeft: {
    R1: [135, 90], R2: [90, 45], L1: [90, 45], L2: [135, 90],
    R3: [98, 143], R4: [53, 8], L3: [56, 11], L4: [97, 142],
  },
  runTurnRight: {
    R1: [90, 135], R2: [45, 90], L1: [45, 90], L2: [90, 135],
    R3: [98, 143], R4: [53, 8], L3: [56, 11], L4: [97, 142],
  },
};

const nextFunction = {
  runWalkPose: 'runWalkBackward',
  runWalkBackward: 'runTurnLeft',
  runTurnLeft: 'runTurnRight',
  runTurnRight: null,
};

for (const movementPath of movementPaths) {
  const source = fs.readFileSync(movementPath, 'utf8');
  for (const [action, servos] of Object.entries(expected)) {
    const body = functionBody(source, action, nextFunction[action]);
    for (const [servo, angles] of Object.entries(servos)) {
      const actual = servoAngles(body, servo);
      assert.deepEqual(
        actual,
        angles,
        `${path.relative(projectRoot, movementPath)} ${action} ${servo} angle curve changed`,
      );
      assert.equal(
        Math.max(...actual) - Math.min(...actual),
        45,
        `${path.relative(projectRoot, movementPath)} ${action} ${servo} must use a 45-degree range`,
      );
    }
  }
}

console.log('PASS: all four gait actions use synchronized 45-degree servo ranges');

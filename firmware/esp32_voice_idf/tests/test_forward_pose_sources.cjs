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

function blockBody(source, openingMarker) {
  const marker = source.indexOf(openingMarker);
  assert.ok(marker >= 0, `${openingMarker} was not found`);

  const openingBrace = source.indexOf('{', marker);
  assert.ok(openingBrace >= 0, `${openingMarker} has no opening brace`);

  let depth = 0;
  for (let index = openingBrace; index < source.length; index += 1) {
    if (source[index] === '{') depth += 1;
    if (source[index] === '}') depth -= 1;
    if (depth === 0) return source.slice(openingBrace + 1, index);
  }

  assert.fail(`${openingMarker} has no closing brace`);
}

function forwardTimeline(source, cycles = 3) {
  const walk = functionBody(source, 'runWalkPose', 'runWalkBackward');
  const loop = blockBody(walk, 'for (int i = 0; i < walkCycles; i++)');
  const steps = [...loop.matchAll(
    /setServoAngle\((R[1-4]|L[1-4]),\s*(\d+)\)|pressingCheck\("forward",\s*(frameDelay|\d+)\)/g,
  )];
  const commands = [];
  let elapsedMs = 0;

  for (let cycle = 0; cycle < cycles; cycle += 1) {
    for (const step of steps) {
      const [, servo, angle, wait] = step;
      if (servo) {
        commands.push({ time: elapsedMs, servo, angle: Number(angle) });
        elapsedMs += 20; // motorCurrentDelay
      } else {
        elapsedMs += wait === 'frameDelay' ? 100 : Number(wait);
      }
    }
  }

  return { commands, cycleMs: elapsedMs / cycles };
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
    R3: [105, 150], R4: [45, 0], L3: [45, 0], L4: [105, 150],
  },
  runTurnRight: {
    R1: [90, 135], R2: [45, 90], L1: [45, 90], L2: [90, 135],
    R3: [105, 150], R4: [45, 0], L3: [45, 0], L4: [105, 150],
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

  const { commands, cycleMs } = forwardTimeline(source, 2);
  assert.equal(
    cycleMs,
    920,
    `${path.relative(projectRoot, movementPath)} forward gait must keep its 920 ms cycle`,
  );

  const expectedPhase = [
    { time: 0, servo: 'R3', angle: 98 },
    { time: 20, servo: 'L3', angle: 11 },
    { time: 40, servo: 'R4', angle: 8 },
    { time: 140, servo: 'L4', angle: 97 },
    { time: 160, servo: 'L2', angle: 90 },
    { time: 200, servo: 'R1', angle: 135 },
    { time: 320, servo: 'R2', angle: 45 },
    { time: 340, servo: 'L1', angle: 90 },
    { time: 360, servo: 'R4', angle: 53 },
    { time: 460, servo: 'L4', angle: 142 },
    { time: 500, servo: 'R3', angle: 143 },
    { time: 620, servo: 'L3', angle: 56 },
    { time: 640, servo: 'R2', angle: 90 },
    { time: 660, servo: 'L1', angle: 45 },
    { time: 760, servo: 'L2', angle: 135 },
    { time: 800, servo: 'R1', angle: 90 },
    { time: 820, servo: 'R3', angle: 98 },
  ];
  const expectedTwoCycles = [
    ...expectedPhase,
    ...expectedPhase.map(({ time, servo, angle }) => ({
      time: time + 920, servo, angle,
    })),
  ];
  assert.deepEqual(
    commands,
    expectedTwoCycles,
    `${path.relative(projectRoot, movementPath)} forward gait must match the mirrored two-cycle phase table`,
  );
}

console.log('PASS: all gait ranges are 45 degrees and forward matches the mirrored two-cycle phase table');

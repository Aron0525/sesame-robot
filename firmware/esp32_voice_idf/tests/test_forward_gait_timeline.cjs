#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(
  root,
  'components/sesame_web/include/sesame_web/legacy_movement_sequences.h',
), 'utf8');

function functionBody(name, nextName) {
  const start = source.indexOf(`inline void ${name}()`);
  const end = source.indexOf(`inline void ${nextName}()`, start);
  assert.ok(start >= 0 && end > start, `${name} body was not found`);
  return source.slice(start, end);
}

function writes(body) {
  return [...body.matchAll(/setServoAngle\((R[1-4]|L[1-4]),\s*(\d+)\)/g)]
    .map(([, servo, angle]) => `${servo}=${angle}`);
}

function commands(body) {
  const pattern = /setServoAngle\((R[1-4]|L[1-4]),\s*(\d+)\)|pressingCheck\("forward",\s*(frameDelay|\d+)\)/g;
  return [...body.matchAll(pattern)].map((match) => {
    if (match[1]) return {type: 'set', servo: match[1], angle: Number(match[2])};
    return {type: 'wait', ms: match[3] === 'frameDelay' ? 100 : Number(match[3])};
  });
}

const forward = functionBody('runWalkPose', 'runWalkBackward');
const loopStart = forward.indexOf('for (int i = 0; i < walkCycles; i++) {');
const loopEnd = forward.indexOf('  runStandPose(1);', loopStart);
assert.ok(loopStart >= 0 && loopEnd > loopStart, 'forward loop was not found');

const bootstrap = forward.slice(0, loopStart);
const loop = forward.slice(loopStart, loopEnd);
const zeroState = {
  R1: 90, R2: 90, L1: 45, L2: 135,
  R4: 53, R3: 98, L3: 56, L4: 142,
};

assert.deepEqual(
  writes(bootstrap),
  ['R1=90', 'R2=90', 'L1=45', 'L2=135', 'R4=53', 'R3=98', 'L3=56', 'L4=142'],
  'the first forward cycle must explicitly start from the confirmed 0 ms pose',
);

let elapsedMs = 0;
const state = {...zeroState};
const samples = new Map();
for (const command of commands(loop)) {
  if (command.type === 'set') {
    state[command.servo] = command.angle;
    samples.set(elapsedMs, {...state});
    elapsedMs += 20;
  } else {
    elapsedMs += command.ms;
  }
}
samples.set(elapsedMs, {...state});

assert.equal(elapsedMs, 920, 'one forward cycle must last 920 ms');

const expectedSamples = new Map([
  [0,   {R1: 90,  R2: 90, L1: 45, L2: 135, R4: 53, R3: 98,  L3: 56, L4: 142}],
  [20,  {R1: 90,  R2: 90, L1: 45, L2: 135, R4: 53, R3: 98,  L3: 11, L4: 142}],
  [40,  {R1: 90,  R2: 90, L1: 45, L2: 135, R4: 8,  R3: 98,  L3: 11, L4: 142}],
  [140, {R1: 90,  R2: 90, L1: 45, L2: 135, R4: 8,  R3: 98,  L3: 11, L4: 97}],
  [160, {R1: 90,  R2: 90, L1: 45, L2: 90,  R4: 8,  R3: 98,  L3: 11, L4: 97}],
  [200, {R1: 135, R2: 90, L1: 45, L2: 90,  R4: 8,  R3: 98,  L3: 11, L4: 97}],
  [320, {R1: 135, R2: 45, L1: 45, L2: 90,  R4: 8,  R3: 98,  L3: 11, L4: 97}],
  [340, {R1: 135, R2: 45, L1: 90, L2: 90,  R4: 8,  R3: 98,  L3: 11, L4: 97}],
  [360, {R1: 135, R2: 45, L1: 90, L2: 90,  R4: 53, R3: 98,  L3: 11, L4: 97}],
  [460, {R1: 135, R2: 45, L1: 90, L2: 90,  R4: 53, R3: 98,  L3: 11, L4: 142}],
  [500, {R1: 135, R2: 45, L1: 90, L2: 90,  R4: 53, R3: 143, L3: 11, L4: 142}],
  [620, {R1: 135, R2: 45, L1: 90, L2: 90,  R4: 53, R3: 143, L3: 56, L4: 142}],
  [640, {R1: 135, R2: 90, L1: 90, L2: 90,  R4: 53, R3: 143, L3: 56, L4: 142}],
  [660, {R1: 135, R2: 90, L1: 45, L2: 90,  R4: 53, R3: 143, L3: 56, L4: 142}],
  [760, {R1: 135, R2: 90, L1: 45, L2: 135, R4: 53, R3: 143, L3: 56, L4: 142}],
  [800, {R1: 90,  R2: 90, L1: 45, L2: 135, R4: 53, R3: 143, L3: 56, L4: 142}],
  [820, {R1: 90,  R2: 90, L1: 45, L2: 135, R4: 53, R3: 98,  L3: 56, L4: 142}],
  [920, zeroState],
]);

assert.deepEqual([...samples], [...expectedSamples],
  'each forward timestamp must match the confirmed servo-angle matrix');

console.log('PASS: forward gait matches the confirmed 920 ms angle timeline');

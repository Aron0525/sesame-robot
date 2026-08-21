#!/usr/bin/env node

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const idfRoot = path.join(__dirname, '..');
const repositoryRoot = path.join(idfRoot, '..', '..');

function readSource(...segments) {
  return fs.readFileSync(path.join(repositoryRoot, ...segments), 'utf8');
}

function section(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start);
  assert.ok(start >= 0 && end > start, `section ${startMarker} was not found`);
  return source.slice(start, end);
}

const actionsToRemove = ['point', 'bow'];

const catalog = readSource(
  'firmware', 'esp32_voice_idf', 'components', 'sesame_robot', 'include',
  'sesame_robot', 'control_catalog.h',
);
const actionCatalog = section(catalog, 'kWebActions', 'kWebExpressions');
for (const action of actionsToRemove) {
  assert.doesNotMatch(actionCatalog, new RegExp(`"${action}"`));
}

const idfSequences = readSource(
  'firmware', 'esp32_voice_idf', 'components', 'sesame_web', 'include',
  'sesame_web', 'legacy_movement_sequences.h',
);
const arduinoSequences = readSource(
  'firmware', 'arduino', 'Sesame_Robot_WiFi_Controller',
  'movement-sequences.h',
);
for (const source of [idfSequences, arduinoSequences]) {
  assert.doesNotMatch(source, /runPointPose/);
  assert.doesNotMatch(source, /runBowPose/);
}

const idfRunner = readSource(
  'firmware', 'esp32_voice_idf', 'components', 'sesame_web',
  'legacy_motion_runner.cpp',
);
assert.doesNotMatch(idfRunner, /action == "point"/);
assert.doesNotMatch(idfRunner, /action == "bow"/);

const idfPortal = readSource(
  'firmware', 'esp32_voice_idf', 'components', 'sesame_web', 'include',
  'sesame_web', 'captive_portal_html.h',
);
const arduinoPortal = readSource(
  'firmware', 'arduino', 'Sesame_Robot_WiFi_Controller', 'captive-portal.h',
);
for (const source of [idfPortal, arduinoPortal]) {
  assert.doesNotMatch(source, /pose\('point'\)/);
  assert.doesNotMatch(source, /pose\('bow'\)/);
}

const arduinoController = readSource(
  'firmware', 'arduino', 'Sesame_Robot_WiFi_Controller',
  'Sesame_Robot_WiFi_Controller.ino',
);
assert.doesNotMatch(arduinoController, /"rn pt"/);
assert.doesNotMatch(arduinoController, /"rn bw"/);
assert.doesNotMatch(arduinoController, /runPointPose/);
assert.doesNotMatch(arduinoController, /runBowPose/);

const gatewayApp = readSource(
  'gateway', 'apps', 'voice_gateway', 'src', 'sesame_voice_gateway', 'app.py',
);
const gatewayPolicy = readSource(
  'gateway', 'apps', 'voice_gateway', 'src', 'sesame_voice_gateway', 'policy.py',
);
for (const actionList of [
  section(gatewayApp, 'REMOTE_ACTIONS', 'REMOTE_EXPRESSIONS'),
  section(gatewayPolicy, 'ALLOWED_ACTIONS', 'ALLOWED_EXPRESSIONS'),
]) {
  for (const action of actionsToRemove) {
    assert.doesNotMatch(actionList, new RegExp(`"${action}"`));
  }
}

const consolePage = readSource(
  'gateway', 'apps', 'voice_gateway', 'src', 'sesame_voice_gateway', 'console.html',
);
const consoleActions = section(consolePage, 'const actions =', 'const poseActions');
for (const action of actionsToRemove) {
  assert.doesNotMatch(consoleActions, new RegExp(`'${action}'`));
}
const gamepadBindings = section(consolePage, 'const gamepadBindings =', 'const releaseToStop');
assert.doesNotMatch(gamepadBindings, /:'point'/);
assert.doesNotMatch(gamepadBindings, /:'bow'/);

const responseSchema = JSON.parse(readSource(
  'contracts', 'schemas', 'agent-response.v1.schema.json',
));
const responseActions = responseSchema.properties.actions.items.properties.name.enum;
const responseExpressions = responseSchema.properties.expression.properties.name.enum;
for (const action of actionsToRemove) {
  assert.ok(!responseActions.includes(action), `${action} must not be a response action`);
  assert.ok(responseExpressions.includes(action), `${action} must remain a face`);
}

console.log('PASS: point and bow remain face-only and cannot be invoked as motions');

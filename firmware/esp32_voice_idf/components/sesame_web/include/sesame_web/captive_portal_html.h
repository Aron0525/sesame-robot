#pragma once

#include <Arduino.h>

// ======================================================================
// --- WEB INTERFACE HTML ---
// ======================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Sesame Access Point Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <style>
    :root {
      --content-color: #ff8c42;
      --content-color-dark: #e67a30;
      --content-color-darker: #cc6b29;
      --content-color-glow: rgba(255, 140, 66, 0.3);
    }
    
    * {
      user-select: none;
      -webkit-user-select: none;
      -webkit-touch-callout: none;
    }
    
    body { 
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; 
      text-align: center; 
      background: linear-gradient(135deg, #0a0a0a, #1a1a2e);
      color: #e0e0e0; 
      touch-action: manipulation; 
      margin: 0;
      padding: 10px;
      overflow-x: hidden;
      box-sizing: border-box;
    }
    
    h2 { 
      margin: 10px 0 20px 0;
      color: #fff;
      font-size: 32px;
      font-weight: 600;
      text-shadow: 0 2px 4px rgba(0,0,0,0.5);
    }
    
    /* Command Queue Status */
    .command-queue {
      font-size: 12px;
      color: #888;
      margin-bottom: 20px;
    }
    .command-queue.full {
      color: #ff6b6b;
      font-weight: bold;
    }
    .control-status {
      color: #aaa;
      font-size: 12px;
      min-height: 18px;
      margin: -12px 0 20px;
    }
    .control-status.success { color: #7be495; }
    .control-status.error { color: #ff8a8a; }
    
    /* Section Containers */
    .sections-container {
      display: flex;
      flex-direction: column;
      gap: 15px;
      max-width: 1400px;
      margin: 0 auto;
    }
    
    .section {
      background: rgba(30, 30, 30, 0.8);
      border: 1px solid #333;
      border-radius: 16px;
      padding: 15px;
      margin: 0 auto;
      width: calc(100% - 20px);
      max-width: 450px;
      box-shadow: 0 4px 20px rgba(0,0,0,0.3);
      box-sizing: border-box;
    }
    
    .section-title {
      font-size: 16px;
      font-weight: 600;
      color: var(--content-color);
      margin: 0 0 15px 0;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    
    /* Button Base Styles */
    button { 
      background: linear-gradient(145deg, #3a3a3a, #2a2a2a);
      border: none; 
      color: #e0e0e0; 
      padding: 15px; 
      font-size: 18px; 
      border-radius: 12px; 
      cursor: pointer; 
      box-shadow: 0 4px 8px rgba(0,0,0,0.3);
      transition: all 0.1s;
      font-weight: 500;
    }
    button:active { 
      box-shadow: 0 2px 4px rgba(0,0,0,0.3);
      transform: translateY(2px); 
    }
    
    /* D-Pad Controls */
    .dpad-container { 
      display: flex; 
      flex-direction: column;
      align-items: center;
      gap: 15px;
      width: 100%;
    }
    .dpad { 
      display: grid; 
      grid-template-columns: repeat(3, 1fr); 
      grid-template-rows: repeat(2, 1fr); 
      gap: 12px;
      width: 100%;
      max-width: 294px;
      aspect-ratio: 3 / 2;
    }
    .dpad button { 
      font-size: 35px; 
      border: 2px solid #555; 
      color: #fff;
      width: 100%;
      height: 100%;
      min-height: 70px;
      touch-action: none;
    }
    .movement-hint {
      color: #aaa;
      font-size: 12px;
      line-height: 1.45;
      margin: -5px 0 0;
      text-align: center;
    }
    .spacer { 
      visibility: hidden; 
    }
    
    /* Pose Grid */
    .grid { 
      display: grid; 
      grid-template-columns: repeat(3, 1fr); 
      gap: 10px; 
    }
    .btn-pose { 
      background: linear-gradient(145deg, var(--content-color), var(--content-color-dark));
      padding: 12px 8px;
      font-size: 15px;
    }
    .btn-pose:active { 
      background: linear-gradient(145deg, var(--content-color-dark), var(--content-color-darker));
    }
    .catalog-hint {
      color: #aaa;
      font-size: 12px;
      line-height: 1.45;
      margin: -5px 0 12px;
    }
    .control-list-scroll {
      max-height: min(44vh, 320px);
      overflow-y: auto;
      overflow-x: hidden;
      touch-action: pan-y;
      overscroll-behavior: contain;
      -webkit-overflow-scrolling: touch;
      padding: 2px 6px 2px 0;
    }
    .btn-direction { border: 1px solid #ffd0ad; }

    /* OLED Face Controls */
    .face-hint {
      color: #aaa;
      font-size: 12px;
      line-height: 1.45;
      margin: -5px 0 14px;
    }
    .face-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
    }
    .btn-face {
      background: linear-gradient(145deg, #3a3a3a, #272727);
      border: 1px solid #555;
      font-size: 14px;
      padding: 12px 8px;
    }
    .btn-face:hover:not(:disabled) {
      border-color: var(--content-color);
    }
    .btn-face.selected {
      background: linear-gradient(145deg, var(--content-color), var(--content-color-dark));
      border-color: #ffd0ad;
      box-shadow: 0 0 0 2px var(--content-color-glow), 0 4px 10px rgba(0,0,0,0.35);
      color: #fff;
    }
    .btn-face:disabled {
      cursor: wait;
      opacity: 0.55;
    }
    .face-status {
      color: #aaa;
      font-size: 12px;
      min-height: 18px;
      margin: 14px 0 0;
    }
    .face-status.success { color: #7be495; }
    .face-status.error { color: #ff8a8a; }
    
    /* Special Buttons */
    .btn-stop-all { 
      background: linear-gradient(145deg, #e63946, #c92a35);
      width: 100%; 
      font-size: 20px; 
      padding: 18px; 
      box-shadow: 0 6px 12px rgba(230, 57, 70, 0.4);
      border: 2px solid #ff6b6b;
      color: #fff;
      text-transform: uppercase;
      letter-spacing: 2px;
    }
    .btn-stop-all:active { 
      background: linear-gradient(145deg, #c92a35, #a8222c);
      transform: translateY(3px); 
    }
    
    .btn-settings { 
      background: linear-gradient(145deg, #555, #444);
      padding: 12px 25px;
      font-size: 16px;
    }
    
    /* Motor Controls */
    .lock-indicator {
      font-size: 11px;
      color: #ff6b6b;
      text-align: center;
      margin-top: 5px;
      display: none;
    }
    .lock-indicator.active {
      display: block;
    }
    
    .motor-controls {
      margin-top: 10px;
    }
    .motor-slider {
      margin: 15px 0;
    }
    .motor-slider label {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
      color: #aaa;
      margin-bottom: 5px;
    }
    .motor-slider input[type="range"] {
      width: 100%;
      height: 6px;
      background: #333;
      border-radius: 5px;
      outline: none;
      -webkit-appearance: none;
    }
    .motor-slider input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 18px;
      height: 18px;
      background: var(--content-color);
      border-radius: 50%;
      cursor: pointer;
      box-shadow: 0 2px 6px var(--content-color-glow);
    }
    .motor-slider input[type="range"]::-moz-range-thumb {
      width: 18px;
      height: 18px;
      background: var(--content-color);
      border-radius: 50%;
      cursor: pointer;
      border: none;
      box-shadow: 0 2px 6px var(--content-color-glow);
    }
    .motor-slider input[type="range"]:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .motor-slider input[type="range"]:disabled::-webkit-slider-thumb,
    .motor-slider input[type="range"]:disabled::-moz-range-thumb {
      background: #666;
      cursor: not-allowed;
    }
    
    /* Gamepad Status */
    .gamepad-status {
      font-size: 13px;
      padding: 8px 14px;
      border-radius: 10px;
      border: 2px solid #666;
      color: #ccc;
      background: rgba(26, 26, 26, 0.8);
      display: inline-block;
    }
    .gamepad-status.connected {
      border-color: #2ecc71;
      color: #2ecc71;
      background: rgba(46, 204, 113, 0.1);
    }
    
    /* Settings Panel */
    .settings-panel { 
      display: none; 
      position: fixed; 
      top: 0; 
      left: 0; 
      width: 100%; 
      height: 100%; 
      background: rgba(0,0,0,0.9); 
      z-index: 100; 
      backdrop-filter: blur(8px);
      overflow-y: auto;
    }
    .settings-content { 
      background: linear-gradient(145deg, #1e1e1e, #2a2a2a);
      border: 1px solid #444; 
      max-width: 400px; 
      margin: 30px auto; 
      padding: 25px; 
      border-radius: 20px; 
      text-align: left; 
      box-shadow: 0 10px 40px rgba(0,0,0,0.6); 
    }
    .settings-content h3 { 
      color: var(--content-color); 
      margin-top: 0; 
      text-align: center;
      font-size: 24px;
    }
    .settings-section {
      margin: 20px 0;
      padding: 15px;
      background: rgba(0,0,0,0.3);
      border-radius: 10px;
    }
    .settings-section h4 {
      color: var(--content-color);
      margin: 0 0 10px 0;
      font-size: 14px;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .settings-content label { 
      display: block; 
      margin-top: 12px; 
      font-weight: 500; 
      color: #ccc;
      font-size: 13px;
    }
    .settings-content input, 
    .settings-content select { 
      width: 100%; 
      padding: 10px; 
      margin-top: 5px; 
      background: #333; 
      color: #fff; 
      border: 1px solid #555; 
      border-radius: 8px; 
      box-sizing: border-box;
      font-size: 14px;
    }
    .btn-save { 
      background: linear-gradient(145deg, #2ecc71, #27ae60);
      box-shadow: 0 4px 8px rgba(46, 204, 113, 0.3);
      width: 100%; 
      margin-top: 25px; 
      color: #fff; 
    }
    .btn-close { 
      background: linear-gradient(145deg, #e74c3c, #c0392b);
      box-shadow: 0 4px 8px rgba(231, 76, 60, 0.3);
      width: 100%; 
      margin-top: 12px; 
      color: #fff; 
    }
    
    /* Desktop Layout */
    @media (min-width: 1024px) {
      body {
        padding: 15px;
      }
      .section {
        padding: 20px;
        width: 100%;
      }
      h2 {
        margin-bottom: 30px;
      }
      .command-queue {
        margin-bottom: 30px;
      }
      .sections-container {
        flex-direction: row;
        justify-content: center;
        align-items: flex-start;
        gap: 50px;
        padding: 0 20px;
      }
      .section-column {
        flex: 0 1 450px;
        display: flex;
        flex-direction: column;
        gap: 20px;
      }
      .section {
        width: 100%;
        max-width: 450px;
        margin: 0;
      }
    }
  </style>
</head>
<body>
  <h2>Sesame Controller</h2>
  <div class="command-queue" id="queueStatus">Command Queue: 0/3</div>
  <div class="control-status" id="controlStatus" role="status" aria-live="polite">Ready</div>
  
  <div class="sections-container">
    <div class="section-column">
      <!-- Movement Control Section -->
      <div class="section">
    <div class="section-title">Movement Control</div>
    <p class="movement-hint">Press and hold a direction to move. Release to stop.</p>
    <div class="dpad-container">
      <div class="dpad">
        <div class="spacer"></div>
        <button type="button" data-direction="forward" aria-label="Move forward">&#9650;</button>
        <div class="spacer"></div>
        
        <button type="button" data-direction="left" aria-label="Move left">&#9664;</button>
        <button type="button" data-direction="backward" aria-label="Move backward">&#9660;</button>
        <button type="button" data-direction="right" aria-label="Move right">&#9654;</button>
      </div>
      <button class="btn-stop-all" onclick="stop()">STOP ALL</button>
    </div>
  </div>

      <!-- Poses & Animations Section -->
      <div class="section">
        <div class="section-title">All Actions</div>
        <p class="catalog-hint">Scroll vertically to browse all actions. Direction buttons move only while held.</p>
        <div class="control-list-scroll" role="region" aria-label="All robot actions" tabindex="0">
          <div class="grid" id="actionControls"></div>
        </div>
      </div>
    </div>

    <div class="section-column">
      <!-- OLED Face Control Section -->
      <div class="section">
        <div class="section-title">OLED Face Control</div>
        <p class="face-hint">Changes the display only. Scroll vertically to browse every usable OLED face.</p>
        <div class="control-list-scroll" role="region" aria-label="All OLED faces" tabindex="0">
          <div class="face-grid" id="faceControls"></div>
        </div>
        <div id="faceStatus" class="face-status" role="status" aria-live="polite">Loading current face...</div>
      </div>

      <!-- Settings & Status Section -->
      <div class="section">
        <div class="section-title">System</div>
        <button class="btn-settings" onclick="openSettings()">Settings</button>
        <div style="margin-top: 15px;">
          <div id="gamepadStatus" class="gamepad-status">Gamepad disconnected</div>
        </div>
      </div>
    </div>
  </div>

  <div id="settingsPanel" class="settings-panel">
    <div class="settings-content">
      <h3>Settings</h3>
      
      <div class="settings-section">
        <h4>Animation Parameters</h4>
        <label>Frame Delay (ms):</label>
        <input type="number" id="frameDelay" min="1" max="1000" step="1">
        <label>Walk Cycles:</label>
        <input type="number" id="walkCycles" min="1" max="50" step="1">
      </div>

      <div class="settings-section">
        <h4>Motor Settings</h4>
        <label>Motor Current Delay (ms):</label>
        <input type="number" id="motorCurrentDelay" min="0" max="500" step="1">
        <label>Motor Speed:</label>
        <select id="motorSpeed">
          <option value="slow">Slow</option>
          <option value="medium" selected>Medium</option>
          <option value="fast">Fast</option>
        </select>
      </div>

      <div class="settings-section">
        <h4>Theme</h4>
        <label>Accent Color:</label>
        <select id="themeColor">
          <option value="#ff8c42">Orange (Default)</option>
          <option value="#66d9ef">Cyan</option>
          <option value="#a8dadc">Light Blue</option>
          <option value="#2ecc71">Green</option>
          <option value="#e74c3c">Red</option>
          <option value="#9b59b6">Purple</option>
          <option value="#f39c12">Yellow</option>
          <option value="#e91e63">Pink</option>
          <option value="custom">Custom</option>
        </select>
        <input type="color" id="customColor" value="#ff8c42" style="margin-top: 10px; display: none;">
      </div>

      <button class="btn-settings" style="width: 100%; margin-top: 20px;" onclick="openMotorControl()">Manual Motor Control</button>

      <button class="btn-save" onclick="saveSettings()">Save Settings</button>
      <button class="btn-close" onclick="closeSettings()">Close</button>
    </div>
  </div>

  <div id="motorControlPanel" class="settings-panel">
    <div class="settings-content">
      <h3>Manual Motor Control</h3>
      <div class="lock-indicator" id="lockIndicator">Locked during animations</div>
      
      <div class="settings-section">
        <div class="motor-controls">
          <div class="motor-slider">
            <label><span>S0 R1</span><span id="m1val">90&deg;</span></label>
            <input type="range" id="motor1" min="0" max="180" value="90" oninput="updateMotor(1, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S1 R2</span><span id="m2val">90&deg;</span></label>
            <input type="range" id="motor2" min="0" max="180" value="90" oninput="updateMotor(2, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S2 L1</span><span id="m3val">90&deg;</span></label>
            <input type="range" id="motor3" min="0" max="180" value="90" oninput="updateMotor(3, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S3 L2</span><span id="m4val">90&deg;</span></label>
            <input type="range" id="motor4" min="0" max="180" value="90" oninput="updateMotor(4, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S4 R4</span><span id="m5val">90&deg;</span></label>
            <input type="range" id="motor5" min="0" max="180" value="90" oninput="updateMotor(5, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S5 R3</span><span id="m6val">90&deg;</span></label>
            <input type="range" id="motor6" min="0" max="180" value="90" oninput="updateMotor(6, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S6 L3</span><span id="m7val">90&deg;</span></label>
            <input type="range" id="motor7" min="0" max="180" value="90" oninput="updateMotor(7, this.value)">
          </div>
          <div class="motor-slider">
            <label><span>S7 L4</span><span id="m8val">90&deg;</span></label>
            <input type="range" id="motor8" min="0" max="180" value="90" oninput="updateMotor(8, this.value)">
          </div>
        </div>
      </div>

      <button class="btn-close" onclick="closeMotorControl()">Close</button>
    </div>
  </div>

<script>
// Command queue management - max 3 commands
let commandQueue = 0;
const MAX_COMMANDS = 3;
let motorsLocked = false;
let activeDirection = null;
let activeDirectionStart = null;
let stopInFlight = null;
let activePointerId = null;
let requestedDirection = null;
let directionRequestId = 0;

// Load theme on page load
document.addEventListener('DOMContentLoaded', () => {
  loadTheme();
  void initializeControlPage();
});

async function initializeControlPage() {
  await loadControlCatalog();
  await loadCurrentFace();
}

const DIRECTION_ACTIONS = new Set(['forward', 'backward', 'left', 'right']);

function controlLabel(name) {
  return name.split('_').map((part) =>
    part.charAt(0).toUpperCase() + part.slice(1),
  ).join(' ');
}

function validCatalogNames(values) {
  if (!Array.isArray(values)) return [];
  return [...new Set(values.filter((value) =>
    typeof value === 'string' && /^[a-z_]+$/.test(value),
  ))];
}

function renderActionControls(actions) {
  const container = document.getElementById('actionControls');
  if (!container) return;
  container.replaceChildren();
  actions.forEach((action) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'btn-pose';
    button.dataset.action = action;
    button.textContent = controlLabel(action);
    if (DIRECTION_ACTIONS.has(action)) {
      button.className += ' btn-direction';
      button.dataset.direction = action;
      setupDirectionalButton(button);
    } else {
      button.addEventListener('click', () => pose(action));
    }
    container.append(button);
  });
}

function renderFaceControls(faces) {
  const container = document.getElementById('faceControls');
  if (!container) return;
  container.replaceChildren();
  faces.forEach((face) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'btn-face';
    button.dataset.face = face;
    button.setAttribute('aria-pressed', 'false');
    button.textContent = controlLabel(face);
    button.addEventListener('click', () => { void setWebFace(face); });
    container.append(button);
  });
}

async function loadControlCatalog() {
  const actionContainer = document.getElementById('actionControls');
  const faceContainer = document.getElementById('faceControls');
  try {
    const response = await fetch('/api/catalog');
    if (!response.ok) throw new Error('catalog request failed');
    const catalog = await response.json();
    const actions = validCatalogNames(catalog.actions);
    const faces = validCatalogNames(catalog.faces);
    if (actions.length === 0 || faces.length === 0) {
      throw new Error('catalog contains no usable controls');
    }
    renderActionControls(actions);
    renderFaceControls(faces);
    setControlStatus(
      `Loaded ${actions.length} actions and ${faces.length} OLED faces.`,
      'success',
    );
  } catch (error) {
    console.log('Control catalog unavailable:', error);
    if (actionContainer) actionContainer.textContent = 'Action library unavailable.';
    if (faceContainer) faceContainer.textContent = 'Face library unavailable.';
    setControlStatus('Control library unavailable. Flash the V3 main firmware first.', 'error');
  }
}

function loadTheme() {
  const savedColor = localStorage.getItem('themeColor');
  if (savedColor) {
    applyTheme(savedColor);
  }
}

function applyTheme(color) {
  const root = document.documentElement;
  root.style.setProperty('--content-color', color);
  
  // Calculate darker shades
  const rgb = hexToRgb(color);
  if (rgb) {
    const dark = `rgb(${Math.max(0, rgb.r - 20)}, ${Math.max(0, rgb.g - 20)}, ${Math.max(0, rgb.b - 20)})`;
    const darker = `rgb(${Math.max(0, rgb.r - 40)}, ${Math.max(0, rgb.g - 40)}, ${Math.max(0, rgb.b - 40)})`;
    const glow = `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, 0.3)`;
    
    root.style.setProperty('--content-color-dark', dark);
    root.style.setProperty('--content-color-darker', darker);
    root.style.setProperty('--content-color-glow', glow);
  }
}

function hexToRgb(hex) {
  const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
  return result ? {
    r: parseInt(result[1], 16),
    g: parseInt(result[2], 16),
    b: parseInt(result[3], 16)
  } : null;
}

function updateQueueStatus() {
  const queueEl = document.getElementById('queueStatus');
  queueEl.textContent = `Command Queue: ${commandQueue}/${MAX_COMMANDS}`;
  if (commandQueue >= MAX_COMMANDS) {
    queueEl.classList.add('full');
  } else {
    queueEl.classList.remove('full');
  }
}

function canSendCommand() {
  return commandQueue < MAX_COMMANDS;
}

function setControlStatus(message, state = '') {
  const status = document.getElementById('controlStatus');
  if (!status) return;
  status.textContent = message;
  status.className = 'control-status' + (state ? ' ' + state : '');
}

async function sendControlCommand(url, pendingMessage, successMessage) {
  setControlStatus(pendingMessage);
  try {
    const response = await fetch(url);
    let payload = null;
    try {
      payload = await response.json();
    } catch (_) {
      // An invalid response is reported below with the HTTP failure.
    }
    if (!response.ok || !payload || payload.ok !== true) {
      throw new Error((payload && payload.message) || 'command rejected');
    }
    setControlStatus(payload.message || successMessage, 'success');
  } catch (error) {
    const message = error && error.message ? error.message : 'network request failed';
    console.log('Control command failed:', error);
    setControlStatus('Command failed: ' + message, 'error');
  }
}

function incrementQueue() {
  commandQueue++;
  updateQueueStatus();
  setTimeout(() => {
    if (commandQueue > 0) {
      commandQueue--;
    }
    updateQueueStatus();
  }, 1000);
}

function lockMotors(duration = 3000) {
  motorsLocked = true;
  document.getElementById('lockIndicator').classList.add('active');
  for (let i = 1; i <= 8; i++) {
    const slider = document.getElementById('motor' + i);
    if (slider) slider.disabled = true;
  }
  setTimeout(() => {
    motorsLocked = false;
    document.getElementById('lockIndicator').classList.remove('active');
    for (let i = 1; i <= 8; i++) {
      const slider = document.getElementById('motor' + i);
      if (slider) slider.disabled = false;
    }
  }, duration);
}

function startDirectionalMove(dir) {
  if (!canSendCommand()) return Promise.resolve();
  incrementQueue();
  activeDirection = dir;
  activeDirectionStart = sendControlCommand(
      '/cmd?go=' + encodeURIComponent(dir),
      'Starting movement...', 'movement started');
  return activeDirectionStart;
}

function startRequestedDirection(dir, requestId) {
  if (requestId !== directionRequestId || requestedDirection !== dir) {
    return Promise.resolve();
  }
  return startDirectionalMove(dir);
}

function stopDirectionalMotion(force) {
  if (stopInFlight !== null) return stopInFlight;
  const startRequest = activeDirectionStart;
  const hasActiveDirection = activeDirection !== null || startRequest !== null;
  activeDirection = null;
  activeDirectionStart = null;
  if (!force && !hasActiveDirection) return Promise.resolve();
  const stopRequest = Promise.resolve(startRequest)
      .catch(() => undefined)
      .then(() => sendControlCommand('/cmd?stop=1',
                                     'Stopping all motion...', 'stopped'));
  stopInFlight = stopRequest;
  stopRequest.finally(() => {
    if (stopInFlight === stopRequest) stopInFlight = null;
  });
  return stopRequest;
}

function move(dir) {
  if (requestedDirection === dir && activeDirection === dir) {
    return activeDirectionStart || Promise.resolve();
  }
  requestedDirection = dir;
  const requestId = ++directionRequestId;
  if (activeDirection === dir) return activeDirectionStart || Promise.resolve();
  if (activeDirection !== null) {
    return stopDirectionalMotion(false)
        .then(() => startRequestedDirection(dir, requestId));
  }
  if (stopInFlight !== null) {
    return stopInFlight.then(() => startRequestedDirection(dir, requestId));
  }
  return startRequestedDirection(dir, requestId);
}

function stop() {
  requestedDirection = null;
  ++directionRequestId;
  commandQueue = 0;
  updateQueueStatus();
  return stopDirectionalMotion(true);
}

function releaseDirectionalMotion() {
  requestedDirection = null;
  ++directionRequestId;
  commandQueue = 0;
  updateQueueStatus();
  return stopDirectionalMotion(false);
}

function startDirectionalPointer(event) {
  if (activePointerId !== null) return;
  event.preventDefault();
  activePointerId = event.pointerId;
  if (event.currentTarget.setPointerCapture) {
    event.currentTarget.setPointerCapture(event.pointerId);
  }
  void move(event.currentTarget.dataset.direction);
}

function stopDirectionalPointer(event) {
  if (activePointerId !== event.pointerId) return;
  event.preventDefault();
  activePointerId = null;
  if (event.currentTarget.releasePointerCapture) {
    event.currentTarget.releasePointerCapture(event.pointerId);
  }
  void releaseDirectionalMotion();
}

function stopActiveDirectionalPointer() {
  if (activePointerId === null) return;
  activePointerId = null;
  void releaseDirectionalMotion();
}

function setupDirectionalButton(button) {
  if (button.dataset.directionBound === 'true') return;
  button.dataset.directionBound = 'true';
  button.addEventListener('pointerdown', startDirectionalPointer);
  button.addEventListener('pointerup', stopDirectionalPointer);
  button.addEventListener('pointercancel', stopDirectionalPointer);
  button.addEventListener('lostpointercapture', stopDirectionalPointer);
}

function setupDirectionalControls() {
  document.querySelectorAll('[data-direction]').forEach(setupDirectionalButton);
  window.addEventListener('pointerup', stopActiveDirectionalPointer);
  window.addEventListener('pointercancel', stopActiveDirectionalPointer);
  window.addEventListener('blur', stopActiveDirectionalPointer);
  window.addEventListener('pagehide', stopActiveDirectionalPointer);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) stopActiveDirectionalPointer();
  });
}

setupDirectionalControls();

function pose(name) { 
  if (!canSendCommand()) return;
  incrementQueue();
  lockMotors(3000);
  void sendControlCommand('/cmd?pose=' + encodeURIComponent(name),
                          'Starting pose...', 'movement started');
}

function setFaceStatus(message, state = '') {
  const status = document.getElementById('faceStatus');
  if (!status) return;
  status.textContent = message;
  status.className = 'face-status' + (state ? ' ' + state : '');
}

function setSelectedFace(faceName) {
  document.querySelectorAll('.btn-face').forEach((button) => {
    const selected = button.dataset.face === faceName;
    button.classList.toggle('selected', selected);
    button.setAttribute('aria-pressed', String(selected));
  });
}

function setFaceControlsDisabled(disabled) {
  document.querySelectorAll('.btn-face').forEach((button) => {
    button.disabled = disabled;
  });
}

async function loadCurrentFace() {
  try {
    const response = await fetch('/api/status');
    if (!response.ok) throw new Error('Status request failed');
    const status = await response.json();
    const faceName = status.currentFace || status.expression || 'unknown';
    setSelectedFace(faceName);
    setFaceStatus('OLED face: ' + faceName, 'success');
  } catch (error) {
    console.log('Face status unavailable:', error);
    setFaceStatus('Face API unavailable. Flash the V3 main firmware first.', 'error');
  }
}

async function setWebFace(faceName) {
  setFaceControlsDisabled(true);
  setFaceStatus('Changing OLED face to ' + faceName + '...');

  try {
    const response = await fetch('/api/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ face: faceName })
    });

    const payload = await response.json();
    if (!response.ok || (payload.status !== 'ok' && payload.ok !== true)) {
      throw new Error(payload.error || payload.message || 'Face request failed');
    }

    setSelectedFace(faceName);
    setFaceStatus('OLED face: ' + faceName, 'success');
  } catch (error) {
    console.log('Face command failed:', error);
    setFaceStatus('Face command failed. Check the V3 main firmware and network.', 'error');
  } finally {
    setFaceControlsDisabled(false);
  }
}

function updateMotor(motorNum, value) {
  if (motorsLocked) return;
  document.getElementById('m' + motorNum + 'val').textContent = value + '\u00B0';
  if (!canSendCommand()) return;
  incrementQueue();
  void sendControlCommand('/cmd?motor=' + motorNum + '&value=' + value,
                          'Updating S' + (motorNum - 1) + '...',
                          'Servo updated');
}

function openSettings() {
  fetch('/getSettings').then(r => r.json()).then(data => {
    document.getElementById('frameDelay').value = data.frameDelay || 100;
    document.getElementById('walkCycles').value = data.walkCycles || 10;
    document.getElementById('motorCurrentDelay').value = data.motorCurrentDelay || 20;
    document.getElementById('motorSpeed').value = data.motorSpeed || 'medium';
    
    // Load theme settings
    const savedColor = localStorage.getItem('themeColor') || '#ff8c42';
    const colorSelect = document.getElementById('themeColor');
    const customColorInput = document.getElementById('customColor');
    
    // Check if saved color matches a preset
    let matchFound = false;
    for (let option of colorSelect.options) {
      if (option.value === savedColor) {
        colorSelect.value = savedColor;
        matchFound = true;
        break;
      }
    }
    
    if (!matchFound) {
      colorSelect.value = 'custom';
      customColorInput.value = savedColor;
      customColorInput.style.display = 'block';
    }
    
    document.getElementById('settingsPanel').style.display = 'block';
  }).catch(() => {
    // Fallback if settings endpoint doesn't exist yet
    document.getElementById('frameDelay').value = 100;
    document.getElementById('walkCycles').value = 10;
    document.getElementById('motorCurrentDelay').value = 20;
    
    const savedColor = localStorage.getItem('themeColor') || '#ff8c42';
    document.getElementById('themeColor').value = savedColor;
    
    document.getElementById('settingsPanel').style.display = 'block';
  });
  
  // Add event listener for theme color change
  document.getElementById('themeColor').addEventListener('change', function() {
    const customColorInput = document.getElementById('customColor');
    if (this.value === 'custom') {
      customColorInput.style.display = 'block';
    } else {
      customColorInput.style.display = 'none';
      applyTheme(this.value);
    }
  });
  
  document.getElementById('customColor').addEventListener('input', function() {
    applyTheme(this.value);
  });
}

function closeSettings() { 
  document.getElementById('settingsPanel').style.display = 'none'; 
}

function openMotorControl() {
  document.getElementById('motorControlPanel').style.display = 'block';
}

function closeMotorControl() {
  document.getElementById('motorControlPanel').style.display = 'none';
}

function saveSettings() {
  const fd = document.getElementById('frameDelay').value;
  const wc = document.getElementById('walkCycles').value;
  const mcd = document.getElementById('motorCurrentDelay').value;
  const ms = document.getElementById('motorSpeed').value;
  
  // Save theme color
  const colorSelect = document.getElementById('themeColor');
  const customColorInput = document.getElementById('customColor');
  const themeColor = colorSelect.value === 'custom' ? customColorInput.value : colorSelect.value;
  localStorage.setItem('themeColor', themeColor);
  applyTheme(themeColor);
  
  fetch(`/setSettings?frameDelay=${fd}&walkCycles=${wc}&motorCurrentDelay=${mcd}&motorSpeed=${ms}`)
    .then(() => closeSettings())
    .catch(() => closeSettings());
}

let activeGamepadIndex = null;
let gamepadPollId = null;
let lastButtonStates = [];
let lastAxisDir = { x: 0, y: 0 };
const axisThreshold = 0.5;
const pollIntervalMs = 80;

const buttonBindings = {
  0: () => pose('stand'),   // A / Cross
  1: () => pose('wave'),    // B / Circle
  2: () => pose('dance'),   // X / Square
  3: () => pose('swim'),    // Y / Triangle
  4: () => pose('point'),   // LB / L1
  5: () => pose('pushup'),  // RB / R1
  6: () => pose('bow'),     // LT / L2
  7: () => pose('shake'),   // RT / R2
  8: () => stop(),          // Back / Share
  9: () => pose('rest'),    // Start / Options
  10: () => pose('cute'),   // L3
  11: () => pose('freaky'), // R3
  12: () => move('forward'),// D-pad up
  13: () => move('backward'),// D-pad down
  14: () => move('left'),   // D-pad left
  15: () => move('right'),  // D-pad right
  16: () => stop(),         // Home / PS
  17: () => pose('worm')    // Touchpad / extra
};

const buttonReleaseStop = new Set([12, 13, 14, 15]);

function updateGamepadStatus(connected) {
  const status = document.getElementById('gamepadStatus');
  if (!status) return;
  if (connected) {
    status.textContent = 'Gamepad connected';
    status.classList.add('connected');
  } else {
    status.textContent = 'Gamepad disconnected';
    status.classList.remove('connected');
  }
}

function handleButtonChange(index, pressed) {
  if (pressed) {
    const action = buttonBindings[index];
    if (action) action();
  } else if (buttonReleaseStop.has(index)) {
    stop();
  }
}

function getAxisDirection(x, y) {
  if (Math.abs(x) < axisThreshold && Math.abs(y) < axisThreshold) return { x: 0, y: 0 };
  if (Math.abs(x) > Math.abs(y)) {
    return { x: x > 0 ? 1 : -1, y: 0 };
  }
  return { x: 0, y: y > 0 ? 1 : -1 };
}

function applyAxisDirection(dir) {
  if (dir.x === 1) move('right');
  else if (dir.x === -1) move('left');
  else if (dir.y === 1) move('backward');
  else if (dir.y === -1) move('forward');
  else stop();
}

function pollGamepad() {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  const pad = pads && activeGamepadIndex !== null ? pads[activeGamepadIndex] : null;
  if (!pad) {
    updateGamepadStatus(false);
    return;
  }
  updateGamepadStatus(true);

  if (!lastButtonStates.length) {
    lastButtonStates = pad.buttons.map(b => !!b.pressed);
  }
  pad.buttons.forEach((btn, i) => {
    const pressed = !!btn.pressed;
    if (pressed !== lastButtonStates[i]) {
      handleButtonChange(i, pressed);
      lastButtonStates[i] = pressed;
    }
  });

  const x = pad.axes[0] || 0;
  const y = pad.axes[1] || 0;
  const dir = getAxisDirection(x, y);
  if (dir.x !== lastAxisDir.x || dir.y !== lastAxisDir.y) {
    applyAxisDirection(dir);
    lastAxisDir = dir;
  }
}

window.addEventListener('gamepadconnected', (e) => {
  activeGamepadIndex = e.gamepad.index;
  lastButtonStates = [];
  lastAxisDir = { x: 0, y: 0 };
  updateGamepadStatus(true);
  if (!gamepadPollId) {
    gamepadPollId = setInterval(pollGamepad, pollIntervalMs);
  }
});

window.addEventListener('gamepaddisconnected', (e) => {
  if (activeGamepadIndex === e.gamepad.index) {
    void stop();
    activeGamepadIndex = null;
    lastButtonStates = [];
    lastAxisDir = { x: 0, y: 0 };
    updateGamepadStatus(false);
  }
});

if (navigator.getGamepads) {
  setInterval(() => {
    if (activeGamepadIndex !== null) return;
    const pads = navigator.getGamepads();
    if (!pads) return;
    for (let i = 0; i < pads.length; i++) {
      if (pads[i]) {
        activeGamepadIndex = pads[i].index;
        updateGamepadStatus(true);
        if (!gamepadPollId) {
          gamepadPollId = setInterval(pollGamepad, pollIntervalMs);
        }
        break;
      }
    }
  }, 1000);
}
</script>
</body>
</html>
)rawliteral";

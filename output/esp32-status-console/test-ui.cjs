const { chromium } = require('playwright-core');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, deviceScaleFactor: 1 });
  await page.goto('http://127.0.0.1:4173', { waitUntil: 'networkidle' });
  await page.waitForSelector('#moduleGrid .module');
  if (await page.locator('#moduleGrid .module').count() !== 6) throw new Error('Expected 6 module cards');
  if (await page.locator('#servoList .servo').count() !== 8) throw new Error('Expected 8 servo rows');
  if (await page.locator('#sessionState').innerText() !== '未接入') throw new Error('Dashboard must start without fabricated status');
  if (!(await page.locator('#rssi').innerText()).startsWith('--')) throw new Error('Dashboard must not invent initial metrics');
  await page.screenshot({ path: 'output/esp32-status-console/esp32-status-console-desktop.png', fullPage: true });
  await page.evaluate(() => window.mergeStatus({
    status: 'LISTEN', sessionId: 'sesame-v3-001', generation: 9, uptime: 8231,
    rssi: -54, latency: 26, heap: 286, temperature: 41.8, dbfs: -35.4, peak: 2240,
    waveform: [-0.08, 0.12, 0.31, -0.2, 0.04], heartbeat_ms: 1000,
    servos: [90, 89, 91, 90, 88, 92, 90, 89],
    modules: { wifi: { state: '在线', value: '−54 dBm', health: 'ok' }, servos: { state: '执行中', value: 'wave', health: 'ok' } },
  }));
  if (await page.locator('#sessionState').innerText() !== 'LISTEN') throw new Error('Expected the latest status frame to render');
  if (!(await page.locator('#rssi').innerText()).includes('-54')) throw new Error('Expected current RSSI to render');
  await page.evaluate(() => window.mergeStatus({
    status: 'SPEAK',
    sessionId: '<img id="untrusted-session-id" src="x">',
    generation: 10,
    peak: 123,
    modules: {
      wifi: {
        title: '<img id="untrusted-module-title" src="x">',
        description: '<img id="untrusted-module-description" src="x">',
        state: '<img id="untrusted-module-state" src="x">',
        value: '<img id="untrusted-module-value" src="x">',
      },
    },
    event: { text: '<img id="untrusted-event" src="x">', level: 'warn' },
  }));
  if (await page.locator('[id^="untrusted-"]').count() !== 0) throw new Error('Status payload must render as text, not HTML');
  const invalidFrameAccepted = await page.evaluate(() => window.mergeStatus({ status: 'UNKNOWN', uptime: 'not-a-number', heartbeat_ms: 'not-a-number' }));
  if (invalidFrameAccepted !== false) throw new Error('Invalid status frames must be rejected');
  if (await page.locator('#sessionState').innerText() !== 'SPEAK') throw new Error('Rejected frames must not overwrite the last valid state');
  await page.locator('#socketUrl').fill('ws://192.168.4.1:81/status');
  await page.locator('#connectButton').click();
  if (!(await page.locator('#connectionHint').innerText()).includes('只允许 wss://')) throw new Error('Remote plaintext WebSocket endpoints must be rejected');
  const reconnectResult = await page.evaluate(() => {
    const sockets = [];
    class FakeWebSocket {
      static CONNECTING = 0;
      static OPEN = 1;
      constructor(url) { this.url = url; this.readyState = FakeWebSocket.CONNECTING; sockets.push(this); }
      close() { this.readyState = 3; this.onclose?.(); }
      send() {}
    }
    window.WebSocket = FakeWebSocket;
    const input = document.getElementById('socketUrl');
    const button = document.getElementById('connectButton');
    input.value = 'wss://status.example.test/status';
    button.click();
    const first = sockets[0];
    button.click();
    button.click();
    const second = sockets[1];
    second.readyState = FakeWebSocket.OPEN;
    second.onopen();
    second.onmessage({ data: JSON.stringify({ status: 'IDLE', sessionId: 'fresh-connection', heartbeat_ms: 1000 }) });
    first.onclose?.();
    return {
      connection: document.getElementById('connectionState').innerText,
      session: document.getElementById('sessionState').innerText,
    };
  });
  if (reconnectResult.connection !== '已连接' || reconnectResult.session !== 'IDLE') throw new Error('Stale sockets must not overwrite the latest connection state');
  await page.waitForTimeout(4200);
  if (await page.locator('#sessionState').innerText() !== '数据过期') throw new Error('Expected stale data to be marked instead of looped');
  if (!(await page.locator('#rssi').innerText()).startsWith('--')) throw new Error('Stale metrics must not be shown as current');
  const mobile = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 1 });
  await mobile.goto('http://127.0.0.1:4173', { waitUntil: 'networkidle' });
  await mobile.waitForSelector('#moduleGrid .module');
  const overflow = await mobile.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
  if (overflow) throw new Error('Mobile layout has horizontal overflow');
  await mobile.screenshot({ path: 'output/esp32-status-console/esp32-status-console-mobile.png', fullPage: true });
  console.log('PASS: rendered 6 modules, 8 servos, current status frames, and stale-data protection');
  await browser.close();
})().catch((error) => { console.error(error); process.exit(1); });

const { chromium } = require('playwright-core');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, deviceScaleFactor: 1 });
  await page.goto('http://127.0.0.1:4174', { waitUntil: 'networkidle' });

  if (await page.locator('[data-action]').count() < 5) throw new Error('Expected the control actions to render');
  if (await page.locator('#servoList .servo').count() !== 8) throw new Error('Expected 8 servo rows');
  if (await page.locator('#robotState').innerText() !== '未接入') throw new Error('Dashboard must start without fabricated status');

  await page.evaluate(() => window.mergeStatus({
    status: 'LISTEN', sessionId: 'sesame-v3-001', generation: 9, uptime: 8231,
    rssi: -54, latency: 26, heap: 286, temperature: 41.8, dbfs: -35.4,
    servos: [90, 89, 91, 90, 88, 92, 90, 89], heartbeat_ms: 1000,
    modules: { wifi: { state: '在线', value: '−54 dBm', health: 'ok' }, servos: { state: '执行中', value: 'wave', health: 'ok' } },
  }));
  if (await page.locator('#robotState').innerText() !== 'LISTEN') throw new Error('Expected status frame to render');
  if (!(await page.locator('#rssi').innerText()).includes('-54')) throw new Error('Expected RSSI to render');

  const requests = [];
  await page.route('**/cmd?pose=wave', async (route) => {
    requests.push(route.request().url());
    await route.fulfill({ status: 204 });
  });
  await page.locator('[data-action="wave"]').click();
  if (requests.length !== 1) throw new Error('Expected action command to use the control API');

  await page.evaluate(() => window.mergeStatus({
    status: 'SPEAK', sessionId: '<img id="injected">', heartbeat_ms: 1000,
    modules: { wifi: { state: '<img id="injected-module">', value: 'safe', health: 'ok' } },
  }));
  if (await page.locator('[id^="injected"]').count() !== 0) throw new Error('Status text must not render as HTML');

  await page.locator('#statusUrl').fill('ws://192.168.4.1:81/status');
  await page.locator('#connectStatus').click();
  if (!(await page.locator('#statusHint').innerText()).includes('只允许 wss://')) throw new Error('Remote plaintext WebSocket must be rejected');

  const mobile = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 1 });
  await mobile.goto('http://127.0.0.1:4174', { waitUntil: 'networkidle' });
  if (await mobile.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)) throw new Error('Mobile layout has horizontal overflow');
  await mobile.screenshot({ path: 'output/robot-console/robot-console-mobile.png', fullPage: true });
  await page.screenshot({ path: 'output/robot-console/robot-console-desktop.png', fullPage: true });
  console.log('PASS: robot console controls, status rendering, safety checks, and responsive layout');
  await browser.close();
})().catch((error) => { console.error(error); process.exit(1); });

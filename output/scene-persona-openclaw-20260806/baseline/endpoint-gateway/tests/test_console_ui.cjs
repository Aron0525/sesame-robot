const { chromium } = require('playwright-core');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 }, deviceScaleFactor: 1 });
  await page.goto('http://127.0.0.1:8788', { waitUntil: 'networkidle' });
  await page.waitForFunction(() => document.querySelector('#gatewayState')?.innerText.includes('已连接'));
  if (await page.locator('input[type="url"]').count() !== 0) throw new Error('Console must not require URL input fields');
  if (await page.locator('[data-action]').count() !== 3) throw new Error('Expected three approved action buttons');
  if (await page.locator('[data-expression]').count() !== 3) throw new Error('Expected three approved expression buttons');
  await page.locator('#command').fill('action dance');
  await page.locator('#terminal').press('Enter');
  if (!(await page.locator('#events').innerText()).includes('未知命令')) throw new Error('Terminal must reject unsupported commands locally');
  await page.screenshot({ path: 'endpoint-gateway/tests/console-desktop.png', fullPage: true });

  const mobile = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 1 });
  await mobile.goto('http://127.0.0.1:8788', { waitUntil: 'networkidle' });
  if (await mobile.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)) throw new Error('Mobile console has horizontal overflow');
  await mobile.screenshot({ path: 'endpoint-gateway/tests/console-mobile.png', fullPage: true });
  await browser.close();
  console.log('PASS: console auto-connects, exposes only approved controls, and is responsive');
})().catch((error) => { console.error(error); process.exit(1); });

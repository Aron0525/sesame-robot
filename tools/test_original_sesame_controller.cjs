const { chromium } = require('playwright-core');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 1 });
  const failures = [];
  page.on('pageerror', error => failures.push(error.message));
  await page.goto('http://127.0.0.1:4180', { waitUntil: 'networkidle' });
  await page.waitForSelector('#queueStatus');
  if ((await page.locator('button').count()) < 15) throw new Error('Original controller buttons did not render');
  if (!(await page.locator('h2').innerText()).includes('Sesame')) throw new Error('Original controller heading did not render');
  await page.screenshot({ path: 'output/original-sesame-controller/original-controller-desktop.png', fullPage: true });
  if (failures.length) throw new Error(`Page errors: ${failures.join('; ')}`);
  console.log('PASS: original captive-portal controller rendered locally.');
  await browser.close();
})().catch(error => { console.error(error); process.exit(1); });

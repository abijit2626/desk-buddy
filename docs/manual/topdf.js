const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage();
  const errs = []; p.on('pageerror', e => errs.push(e.message)); p.on('requestfailed', r => errs.push('failed ' + r.url()));
  await p.goto('file://' + process.cwd() + '/manual.html', { waitUntil: 'networkidle' });
  await p.evaluate(() => document.fonts.ready);
  // report any page whose content overflows its box
  const over = await p.evaluate(() => [...document.querySelectorAll('.page')].map((pg, i) => {
    const r = pg.getBoundingClientRect(); let maxB = 0;
    pg.querySelectorAll('*').forEach(el => { if (el.classList.contains('pnum') || el.classList.contains('deco') || el.closest('.deco')) return; const b = el.getBoundingClientRect(); if (b.height) maxB = Math.max(maxB, b.bottom); });
    return { page: i + 1, spare_mm: +(((r.bottom - maxB) / 96) * 25.4).toFixed(1) };
  }));
  console.log(JSON.stringify(over));
  const fontsOk = await p.evaluate(() => [document.fonts.check('600 12px Fredoka'), document.fonts.check('600 12px Nunito')]);
  console.log('fonts', fontsOk, errs.join('|') || 'no errors');
  await p.pdf({ path: 'Desk_Buddy_Manual.pdf', preferCSSPageSize: true, printBackground: true });
  await b.close();
})();

// ---------- status line ----------
// One line along the bottom of the screen carries every message (Functions, Generate, presets) and, when Tooltips is on
// in Options › Monitor, what the control under the pointer does. Messages win over tooltips and fade after a few seconds.
// Tooltips are off unless turned on (v105 turned them off for everyone once: the setting moved to the tb303.tips2 key).
// Every title attribute on the page (menus, dropdowns and Options too) becomes data-tip, so the browser's own tooltip
// boxes never pop up anywhere.
(() => {
  const line = document.getElementById('sl-text'), main = document.querySelector('.main');
  const tipsBtn = document.getElementById('tips-on');
  let tipsOn = store.get('tips2', false), msg = '', msgT = 0, tip = '';
  const show = () => { const t = msg || (tipsOn ? tip : ''); line.textContent = t; line.classList.toggle('msg', !!msg); };
  const say = (t) => { msg = (t || '').trim(); clearTimeout(msgT); if (msg) msgT = setTimeout(() => { msg = ''; show(); }, 5000); show(); };
  // mirror the old popup message spots into the line
  for (const id of ['sq-msg', 'prj-msg']) {
    const el = document.getElementById(id); if (!el) continue;
    new MutationObserver(() => { if (el.textContent.trim()) say(el.textContent); }).observe(el, { childList: true, characterData: true, subtree: true });
  }
  const tipOf = (t) => {
    const el = t?.closest?.('[title], [data-tip]'); if (!el || !main.contains(el)) return '';
    if (el.hasAttribute('title')) { if (el.title) el.dataset.tip = el.title; el.removeAttribute('title'); }
    return el.dataset.tip || '';
  };
  main.addEventListener('pointerover', (e) => { tip = tipOf(e.target); show(); });
  main.addEventListener('pointerleave', () => { tip = ''; show(); });
  main.addEventListener('focusin', (e) => { tip = tipOf(e.target); show(); });
  // strip titles up front too, so a first hover never shows the browser's box
  const root = document.documentElement;
  const strip = () => root.querySelectorAll('body [title]').forEach(el => { if (el.title) el.dataset.tip = el.title; el.removeAttribute('title'); });
  strip(); new MutationObserver(strip).observe(root, { subtree: true, childList: true, attributes: true, attributeFilter: ['title'] });
  const renderTips = () => { tipsBtn.setAttribute('aria-pressed', tipsOn); tipsBtn.textContent = tipsOn ? 'On' : 'Off'; };
  tipsBtn.onclick = () => { tipsOn = !tipsOn; store.set('tips2', tipsOn); renderTips(); show(); };
  renderTips();
})();

// ---------- options sheet ----------
const optDlg = document.getElementById('options');
document.getElementById('open-options').onclick = () => { if (optDlg.showModal) optDlg.showModal(); else optDlg.setAttribute('open', ''); };
document.getElementById('close-options').onclick = () => { if (optDlg.close) optDlg.close(); else optDlg.removeAttribute('open'); };
optDlg.addEventListener('click', (e) => { if (e.target === optDlg) optDlg.close(); });
const otabs = document.getElementById('otabs');
function showSec(k, focus) {
  for (const b of otabs.querySelectorAll('[role=tab]')) { const on = b.dataset.sec === k; b.setAttribute('aria-selected', on); b.tabIndex = on ? 0 : -1; if (on && focus) b.focus(); }
  for (const sec of optDlg.querySelectorAll('.osec')) sec.hidden = sec.id !== 'osec-' + k;
  optDlg.querySelector('.opt-body').scrollTop = 0; store.set('optTab', k);
}
otabs.addEventListener('click', (e) => { const b = e.target.closest('[role=tab]'); if (b) showSec(b.dataset.sec, true); });
otabs.addEventListener('keydown', (e) => {
  const d = { ArrowLeft: -1, ArrowRight: 1 }[e.key]; if (!d) return;
  const t = [...otabs.querySelectorAll('[role=tab]')], i = t.findIndex(b => b.getAttribute('aria-selected') === 'true');
  showSec(t[(i + d + t.length) % t.length].dataset.sec, true); e.preventDefault();
});
showSec(document.getElementById('osec-' + store.get('optTab', 'midi')) ? store.get('optTab', 'midi') : 'midi');

// ---------- Options › Colours: the LCD's glass and ink. Each theme is a set of colour tokens in lcd.css ([data-lcd=…]);
// orange is the default. The choice is saved and put on <html> by a line in the page head before the styles, so it never flashes ----------
(() => {
  const THEMES = [['orange', 'Orange'], ['amber', 'Amber'], ['lime', 'Lime'], ['blue', 'Blue'], ['red', 'Red'], ['grey', 'Grey']];
  const box = document.getElementById('lcd-swatches'), root = document.documentElement;
  const cur = () => THEMES.some(([k]) => k === root.dataset.lcd) ? root.dataset.lcd : 'orange';
  const mark = () => { for (const b of box.children) { const on = b.dataset.lcd === cur(); b.setAttribute('aria-checked', on); b.tabIndex = on ? 0 : -1; } };
  const pick = (k, focus) => {
    if (k === 'orange') delete root.dataset.lcd; else root.dataset.lcd = k;
    store.set('lcdTheme', k); mark(); if (focus) box.querySelector(`[data-lcd="${k}"]`).focus();
  };
  for (const [k, name] of THEMES) {
    const b = document.createElement('button');
    b.type = 'button'; b.className = 'sw'; b.dataset.lcd = k; b.setAttribute('role', 'radio');
    b.innerHTML = `<span>${name}</span><i class="bars" aria-hidden="true"></i>`;
    box.append(b);
  }
  box.addEventListener('click', (e) => { const b = e.target.closest('[data-lcd]'); if (b) pick(b.dataset.lcd); });
  box.addEventListener('keydown', (e) => {
    const d = { ArrowLeft: -1, ArrowRight: 1, ArrowUp: -1, ArrowDown: 1 }[e.key]; if (!d) return;
    const i = THEMES.findIndex(([k]) => k === cur());
    pick(THEMES[(i + d + THEMES.length) % THEMES.length][0], true); e.preventDefault(); e.stopPropagation();
  });
  mark();
})();

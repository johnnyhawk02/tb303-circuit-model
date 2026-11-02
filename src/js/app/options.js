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
showSec(document.getElementById('osec-' + store.get('optTab', 'circuit')) ? store.get('optTab', 'circuit') : 'circuit');
document.getElementById('restore').onclick = () => {
  for (const k in COMPONENTS) { state.parts[k] = COMPONENTS[k].v; preview.setComponent(k, COMPONENTS[k].v); send({ type: 'component', name: k, value: COMPONENTS[k].v }); }
  renderParts(); renderFrontMods(); renderFx(); for (const id in knobEls) drawKnob(id); readouts(); saveSession();
};


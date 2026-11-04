// ---------- Randomise menu and pattern key commands ----------
// Johnny: the randomise and alter buttons live in one menu next to the pattern tools instead of a row on the page,
// and every pattern tool has a key. Keys are plain letters (Shift for the gentler Alter versions), so they never
// clash with 1-8 (pattern slots), Space (play) or Cmd/Ctrl+C/V (copy, paste). Each key is shown in the menu and in the
// status-line tooltip. Keys are ignored while typing in a text field or with Options open.
(() => {
  const mac = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent);
  const cmd = mac ? '⌘' : 'Ctrl+', shift = mac ? '⇧' : 'Shift+';
  const click = (id) => () => { const b = document.getElementById(id); if (b && !b.disabled) b.click(); };
  // id: what it does, its key (code + shift/cmd), and the on-page button that shows the key in its tooltip (if any)
  const ACTS = {
    'r-acc': { run: () => randomise('accent'), code: 'KeyA' },
    'r-sld': { run: () => randomise('slide'), code: 'KeyS' },
    'r-oct': { run: () => randomise('octave'), code: 'KeyO' },
    'a-acc': { run: () => alterLane('accent'), code: 'KeyA', shift: true },
    'a-sld': { run: () => alterLane('slide'), code: 'KeyS', shift: true },
    'a-oct': { run: () => alterLane('octave'), code: 'KeyO', shift: true },
    'alter': { run: () => FUNCTIONS.alter(), code: 'KeyL' },
    'p-shuf': { run: () => shuffleSteps(), code: 'KeyH' },
    'rand': { run: click('rand'), code: 'KeyG', btn: 'rand' },
    'p-refill': { run: click('p-refill'), code: 'KeyG', shift: true, btn: 'p-refill' },
    'p-dup': { run: click('p-dup'), code: 'KeyD', btn: 'p-dup' },
    'p-undo': { run: click('p-undo'), code: 'KeyZ', cmd: true, btn: 'p-undo' },
    'menu': { run: () => rmOpen(rmMenu.hidden), code: 'KeyR', btn: 'rm-btn' },
  };
  const keyName = (a) => (a.cmd ? cmd : '') + (a.shift ? shift : '') + a.code.slice(3);
  const btn = document.getElementById('rm-btn'), rmMenu = document.getElementById('rm-menu');
  function rmOpen(on) {
    rmMenu.hidden = !on; btn.setAttribute('aria-expanded', on);
    if (on) rmMenu.querySelector('button').focus();
  }
  // labels: the key on the right of each menu item, and after the tooltip text everywhere
  for (const [id, a] of Object.entries(ACTS)) {
    const k = keyName(a);
    for (const el of [rmMenu.querySelector(`[data-rm="${id}"]`), a.btn && document.getElementById(a.btn)]) {
      if (!el) continue;
      const tip = el.getAttribute('title') || el.dataset.tip || el.textContent.trim();
      el.title = `${tip} (key ${k})`;
      if (el.closest('#rm-menu')) el.insertAdjacentHTML('beforeend', `<kbd>${k}</kbd>`);
    }
  }
  const pc = document.getElementById('p-copy'), pp = document.getElementById('p-paste');
  pc.title = `Copy this pattern (key ${cmd}C)`; pp.title = `Paste over this pattern (key ${cmd}V)`;
  btn.onclick = () => rmOpen(rmMenu.hidden);
  document.addEventListener('pointerdown', (e) => { if (!rmMenu.hidden && !e.target.closest('.rmenu')) rmOpen(false); });
  rmMenu.addEventListener('click', (e) => { const b = e.target.closest('[data-rm]'); if (!b) return; rmOpen(false); ACTS[b.dataset.rm].run(); });
  rmMenu.addEventListener('keydown', (e) => {
    const items = [...rmMenu.querySelectorAll('button:not(:disabled)')], i = items.indexOf(document.activeElement);
    if (e.key === 'Escape') { e.preventDefault(); rmOpen(false); btn.focus(); }
    else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); items[(i + (e.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length].focus(); }
  });
  document.addEventListener('keydown', (e) => {
    if (e.repeat || e.altKey || e.isComposing) return;
    if (e.target.closest?.('input[type=text], input[type=number], input:not([type]), textarea, select, [contenteditable=""], [contenteditable=true]')) return;
    if (document.getElementById('options').open) return;
    const c = e.metaKey || e.ctrlKey;
    const hit = Object.entries(ACTS).find(([, a]) => a.code === e.code && !!a.shift === e.shiftKey && !!a.cmd === c);
    if (!hit) return;
    e.preventDefault();
    if (hit[0] !== 'menu' && !rmMenu.hidden) rmOpen(false);
    hit[1].run();
  });
})();

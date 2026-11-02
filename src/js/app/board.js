// ---------- circuit board: a stylised PCB showing every part at its live value (just for fun, not the real layout) ----------
const boardEl = document.getElementById('board'), boardInfo = document.getElementById('board-info');
const BOARD_BLOCKS = {
  CV: 'PITCH CV DAC', Slide: 'SLIDE', VCO: 'VCO', Square: 'SQUARE SHAPER', VCF: 'VCF · DIODE LADDER',
  Env: 'FILTER ENVELOPE', Accent: 'ACCENT SWEEP', VCA: 'VCA · OUTPUT', RAT: 'PROCO RAT', '224': 'LEXICON 224',
};
const BOARD_COLS = { CV: 3, Slide: 2, VCO: 6, Square: 3, VCF: 5, Accent: 3, Env: 3, VCA: 7, RAT: 5, '224': 4 };
const POT_KNOB = { VR2: 'tuning', VR4: 'resonance', VR5: 'envmod', VR6: 'decay', VR7: 'accent' };
const BAND = ['#111', '#7a3b12', '#d22', '#f07c1a', '#f2d21b', '#2a9a3a', '#2361d6', '#8a3fc8', '#888', '#f4f4f4'];
const GOLD = '#c9a43a', SILVER = '#c8c8c8';
// colour bands for the nominal value: 4 bands for an E24 part (±5% gold), 5 bands for an in-between value (±1% brown)
function bands(k, v) {
  const nom = snapReal(k, v), e24 = Math.abs(v / nom - 1) <= tolerance(k) + 1e-6;
  const x = e24 ? nom : v, digits = e24 ? 2 : 3;
  const exp = Math.floor(Math.log10(x)) - (digits - 1), m = Math.round(x / Math.pow(10, exp));
  const ds = String(m).padStart(digits, '0').split('').map(Number);
  const mult = exp >= 0 ? BAND[exp] : exp === -1 ? GOLD : SILVER;
  return { cols: [...ds.map(d => BAND[d]), mult, e24 ? GOLD : BAND[1]], prec: !e24 };
}
const CELL_W = 88, CELL_H = 76, PAD = 14, HEAD = 22;
function partSVG(k, cx, cy) {
  const d = COMPONENTS[k], v = state.parts[k], chg = partChanged(k, v);
  const lead = (x1, x2) => `<line class="lead" x1="${x1}" y1="${cy}" x2="${x2}" y2="${cy}"/>`;
  const pads = `<circle class="pad" cx="${cx - 34}" cy="${cy}" r="4"/><circle class="pad" cx="${cx + 34}" cy="${cy}" r="4"/>`;
  let body;
  if (/^VR/.test(k)) {
    const t = POT_KNOB[k] ? state.knobs[POT_KNOB[k]] : toSlider(k, v) / SL, a = (KNOB_A0 + KNOB_SWEEP * t) * Math.PI / 180;
    body = `${pads}${lead(cx - 34, cx + 34)}<rect class="pot" x="${cx - 17}" y="${cy - 17}" width="34" height="34" rx="3"/><circle class="shaft" cx="${cx}" cy="${cy}" r="11"/>`
      + `<line class="slot" x1="${cx}" y1="${cy}" x2="${(cx + Math.sin(a) * 10).toFixed(1)}" y2="${(cy - Math.cos(a) * 10).toFixed(1)}"/>`;
  } else if (d.block === '224') {
    let pins = ''; for (let i = 0; i < 6; i++) pins += `<rect class="pin" x="${cx - 18 + i * 7}" y="${cy - 14}" width="3" height="4"/><rect class="pin" x="${cx - 18 + i * 7}" y="${cy + 10}" width="3" height="4"/>`;
    body = `${pins}<rect class="chip" x="${cx - 22}" y="${cy - 10}" width="44" height="20" rx="2"/><circle class="notch" cx="${cx - 16}" cy="${cy}" r="2.5"/>`;
  } else if (d.unit === 'Ω') {
    const b = bands(k, v), w = 40, x0 = cx - w / 2;
    body = `${pads}${lead(cx - 34, cx + 34)}<rect class="res${b.prec ? ' prec' : ''}" x="${x0}" y="${cy - 7}" width="${w}" height="14" rx="6"/>`
      + b.cols.map((c, i) => `<rect x="${(i < b.cols.length - 1 ? x0 + 7 + i * 5.5 : x0 + w - 9).toFixed(1)}" y="${cy - 7}" width="3" height="14" fill="${c}"/>`).join('');
  } else if (d.unit === 'F') {
    if (d.v >= 1e-6) body = `${pads}${lead(cx - 34, cx + 34)}<circle class="elec" cx="${cx}" cy="${cy}" r="14"/><path class="minus" d="M${cx + 9.9} ${cy - 9.9}A14 14 0 0 1 ${cx + 9.9} ${cy + 9.9}" /><path class="vent" d="M${cx - 6} ${cy}h12M${cx} ${cy - 6}v12"/>`;
    else if (d.v < 1e-9) body = `${pads}${lead(cx - 34, cx + 34)}<circle class="cer" cx="${cx}" cy="${cy}" r="10"/>`;
    else body = `${pads}${lead(cx - 34, cx + 34)}<rect class="film" x="${cx - 13}" y="${cy - 10}" width="26" height="20" rx="3"/>`;
  } else {
    body = `${pads}${lead(cx - 34, cx + 34)}<rect class="trim" x="${cx - 10}" y="${cy - 10}" width="20" height="20" rx="2"/><circle class="tw" cx="${cx}" cy="${cy}" r="6.5"/><path class="tx" d="M${cx - 4} ${cy}h8M${cx} ${cy - 4}v8"/>`;
  }
  const esc = (x) => String(x).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  return `<g class="part${chg ? ' chg' : ''}" data-k="${k}" tabindex="0" role="button" aria-label="${k} ${esc(fmt(v, d.unit))}"><title>${k}: ${esc(fmt(v, d.unit))}${chg ? ` (stock ${esc(fmt(d.v, d.unit))})` : ''}</title>`
    + `<rect class="hit" x="${cx - CELL_W / 2 + 2}" y="${cy - CELL_H / 2 + 2}" width="${CELL_W - 4}" height="${CELL_H - 4}" rx="4"/>${body}`
    + `<text class="ref" x="${cx}" y="${cy - 22}">${esc(k.replace(/^RAT_|^LEX_/, ''))}</text><text class="pv" x="${cx}" y="${cy + 31}">${esc(fmt(v, d.unit))}</text></g>`;
}
// lay blocks out in shelves, signal order, on a board of the given width
function layoutBoard(blocks, W, solo) {
  let x = PAD, y = PAD + 26, rowH = 0, out = '', maxX = 0;
  for (const b of blocks) {
    const keys = Object.keys(COMPONENTS).filter(k => COMPONENTS[k].block === b); if (!keys.length) continue;
    const cols = Math.min(keys.length, BOARD_COLS[b] || 4), rows = Math.ceil(keys.length / cols);
    const bw = cols * CELL_W + 12, bh = rows * CELL_H + HEAD + 8;
    if (x + bw > W - PAD) { x = PAD; y += rowH + 12; rowH = 0; }
    out += solo ? '' : `<rect class="blk" x="${x}" y="${y}" width="${bw}" height="${bh}" rx="6"/><text class="blkname" x="${x + 9}" y="${y + 15}">${BOARD_BLOCKS[b] || b}</text>`;
    keys.forEach((k, i) => {
      const cx = x + 6 + (i % cols) * CELL_W + CELL_W / 2, cy = y + HEAD + Math.floor(i / cols) * CELL_H + CELL_H / 2;
      if (i % cols && COMPONENTS[k].block !== '224') out += `<line class="trace" x1="${cx - CELL_W + 34}" y1="${cy}" x2="${cx - 34}" y2="${cy}"/>`;
      out += partSVG(k, cx, cy);
    });
    maxX = Math.max(maxX, x + bw); x += bw + 12; rowH = Math.max(rowH, bh);
  }
  return { svg: out, h: y + rowH + PAD, w: maxX + PAD };
}
function boardSVG(title, cls, blocks, W) {
  const L = layoutBoard(blocks, W, blocks.length === 1); W = L.w; const holes = [[10, 10], [W - 10, 10], [10, L.h - 10], [W - 10, L.h - 10]];
  return { h: L.h, w: W, svg: `<rect class="pcb ${cls}" x="0" y="0" width="${W}" height="${L.h}" rx="10"/>`
    + holes.map(([x, y]) => `<circle class="hole" cx="${x}" cy="${y}" r="5"/>`).join('')
    + `<text class="silk-title" x="${PAD + 10}" y="${PAD + 12}">${title}</text>${L.svg}` };
}

function renderBoard() {
  const W = 1180, ver = document.getElementById('ver').textContent;
  const main = boardSVG(`TB-303 · MAIN BOARD · MODEL ${ver}`, 'g', ['CV', 'Slide', 'VCO', 'Square', 'VCF', 'Accent', 'Env', 'VCA'], W);
  const rat = boardSVG('PROCO RAT', 'r', ['RAT'], W), lex = boardSVG('LEXICON 224 · DIGITAL', 'l', ['224'], W);
  const y2 = main.h + 18, H = y2 + Math.max(rat.h, lex.h), BW = Math.max(main.w, rat.w + 18 + lex.w);
  boardEl.setAttribute('viewBox', `0 0 ${BW} ${H}`);
  boardEl.innerHTML = main.svg + `<g transform="translate(0 ${y2})">${rat.svg}</g><g transform="translate(${rat.w + 18} ${y2})">${lex.svg}</g>`;
  const cur = boardInfo.dataset.k; if (cur) boardEl.querySelector(`.part[data-k="${cur}"]`)?.classList.add('sel');
}
function boardSay(k) {
  const d = COMPONENTS[k], v = state.parts[k], t = tolerance(k), chg = partChanged(k, v);
  const esc = (x) => String(x).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  boardInfo.dataset.k = k;
  boardInfo.innerHTML = `<b>${k}</b> <span class="bv${chg ? ' changed' : ''}">${esc(fmt(v, d.unit))}</span> · ${esc(d.note)}. Stock ${esc(fmt(d.v, d.unit))}${t ? `, ±${+(t * 100).toFixed(1)}%` : ''}.`
    + (d.fixed ? '' : ` <button type="button" class="mini" id="board-edit">Edit</button>`);
  for (const g of boardEl.querySelectorAll('.part.sel')) g.classList.remove('sel');
  boardEl.querySelector(`.part[data-k="${k}"]`)?.classList.add('sel');
}
boardEl.addEventListener('click', (e) => { const g = e.target.closest('.part'); if (g) boardSay(g.dataset.k); });
boardEl.addEventListener('keydown', (e) => { const g = e.target.closest('.part'); if (g && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); boardSay(g.dataset.k); } });
boardInfo.addEventListener('click', (e) => {
  if (e.target.id !== 'board-edit') return;
  const k = boardInfo.dataset.k; selectTab(COMPONENTS[k].block); showSec('circuit');
  if (optDlg.showModal) optDlg.showModal(); else optDlg.setAttribute('open', '');
  showDetail(k); partsEl.querySelector(`.knob[data-k="${k}"]`)?.focus();
});


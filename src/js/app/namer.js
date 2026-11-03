// ---------- preset auto-namer ----------
// Five styles, often mixed: alien word-part mashups, messed-up desserts, cosmology, ancient Greek-ish,
// and a last pass where each s turns into z 60% of the time.
const NAMER = (() => {
  const pick = (a) => a[Math.floor(Math.random() * a.length)];
  const cap = (w) => w.charAt(0).toUpperCase() + w.slice(1);
  const chance = (p) => Math.random() < p;
  const WORDS = ['squelch', 'acid', 'resonance', 'filter', 'bubble', 'sludge', 'wobble', 'circuit', 'diode', 'ladder', 'voltage', 'quantum',
    'thunder', 'velvet', 'crystal', 'murmur', 'fractal', 'glimmer', 'plasma', 'mollusc', 'saxophone', 'lantern', 'whisper', 'cobalt'];
  const A_PRE = ['zor', 'qua', 'vex', 'thra', 'xil', 'oom', 'kry', 'bla', 'nyx', 'gor', 'fen', 'plu', 'yth', 'ska', 'zub', 'mor', 'quee', 'drax'];
  const A_MID = ['la', 'ri', 'on', 'ex', 'ub', 'ae', 'ix', 'ol', 'ee', 'aa', 'u', 'yo'];
  const A_END = ['th', 'x', 'nok', 'ar', 'ium', 'ozz', 'eep', 'ul', 'orb', 'ak', 'ish', 'oid'];
  // front of one word up to a vowel, back of another from a consonant, so the joint stays sayable
  const splice = (a, b) => {
    const cuts = [...a.slice(1, -1)].map((c, i) => (/[aeiouy]/.test(c) ? i + 2 : 0)).filter(Boolean);
    const tails = [...b.slice(1)].map((c, i) => (!/[aeiouy]/.test(c) ? i + 1 : 0)).filter(i => i && i >= b.length / 3);
    return a.slice(0, cuts.length ? pick(cuts) : 2) + b.slice(tails.length ? pick(tails) : b.length >> 1);
  };
  const alien = () => {
    if (chance(0.5)) { // splice the front of one word onto the back of another
      const a = pick(WORDS), b = pick(WORDS);
      return cap(splice(a, b));
    }
    let w = pick(A_PRE); if (chance(0.7)) w += pick(A_MID); w += pick(A_END);
    return cap(w) + (chance(0.25) ? '-' + cap(pick(A_PRE) + pick(A_END)) : '');
  };
  const DESSERTS = ['Tiramisu', 'Pavlova', 'Eclair', 'Creme Brulee', 'Baklava', 'Panna Cotta', 'Profiterole', 'Trifle', 'Cheesecake', 'Sorbet',
    'Macaron', 'Strudel', 'Meringue', 'Parfait', 'Clafoutis', 'Mille-feuille', 'Cannoli', 'Gelato', 'Souffle', 'Sticky Toffee', 'Banoffee', 'Tarte Tatin', 'Spotted Dick', 'Jelly'];
  const MESS = ['Burnt', 'Melted', 'Sideways', 'Haunted', 'Soggy', 'Inverted', 'Radioactive', 'Fermented', 'Exploded', 'Backwards', 'Feral', 'Damp', 'Cursed', 'Upside-down'];
  const dessert = () => {
    const d = pick(DESSERTS), r = Math.random();
    if (r < 0.3) return pick(MESS) + ' ' + d;
    if (r < 0.55) return splice(d.replace(/ .*/, ''), pick(DESSERTS).replace(/ .*/, '').toLowerCase()); // Tiramiclair
    if (r < 0.75) return d.replace(/[aeiou]/g, (v) => (chance(0.5) ? { a: 'o', e: 'u', i: 'a', o: 'e', u: 'i' }[v] : v)); // vowels slipped
    if (r < 0.9) { const w = d.split(' '); w[0] = w[0].split('').reverse().join('').toLowerCase(); w[0] = cap(w[0]); return w.join(' '); } // backwards
    return d + ' ' + pick(['Incident', 'Collapse', 'Mishap', 'Disaster', 'Situation']);
  };
  const COSMOS = ['Redshift', 'Event Horizon', 'Dark Matter', 'Inflaton', 'Quasar', 'Pulsar', 'Magnetar', 'Cosmic Web', 'Baryon', 'Hubble Flow',
    'Accretion Disk', 'Neutrino', 'Singularity', 'Nebula', 'Perihelion', 'Aphelion', 'Parsec', 'Lagrange Point', 'Oort Cloud', 'Lensing',
    'Big Crunch', 'Supernova', 'Blueshift', 'Dyson Sphere', 'Wormhole', 'Planck Epoch', 'Cepheid', 'Kuiper Belt', 'Dark Energy', 'Spaghettification'];
  const cosmos = () => pick(COSMOS) + (chance(0.2) ? ' ' + pick(['Prime', 'Remnant', 'Drift', 'Echo', 'Zero']) : '');
  const GREEK = ['Aether', 'Logos', 'Kairos', 'Chronos', 'Pneuma', 'Eidolon', 'Arete', 'Hubris', 'Nous', 'Telos', 'Apeiron', 'Kosmos', 'Chthonia',
    'Eros', 'Thanatos', 'Hypnos', 'Mneme', 'Agora', 'Oracle', 'Labyrinth', 'Amphora', 'Odyssey', 'Hydra', 'Gorgon', 'Elysion', 'Tartaros', 'Delphi', 'Ithaka'];
  const greekify = (w) => {
    const base = w.replace(/[^a-z]+$/i, '').replace(/[aeiouy]+$/i, '');
    return (chance(0.3) ? pick(['Proto', 'Hyper', 'Neo', 'Pan', 'Archi', 'Meta']) : '') + cap(base.toLowerCase()) + pick(['os', 'ia', 'eion', 'ikon', 'ides', 'ax']);
  };
  const greek = () => (chance(0.55) ? pick(GREEK) : greekify(pick([...WORDS, ...GREEK]))) + (chance(0.15) ? ' ' + pick(['of Delphi', 'the Elder', 'Ascending', 'Rex']) : '');
  const STYLES = [alien, dessert, cosmos, greek];
  // mixes: two styles side by side, or a word from one greekified
  const mix = () => {
    const r = Math.random();
    if (r < 0.4) return pick([cosmos, greek, alien])() + ' ' + pick([dessert, cosmos])().split(' ').pop();
    if (r < 0.7) return greekify(pick([...DESSERTS, ...COSMOS]).split(' ')[0]);
    return pick(STYLES)() + ' ' + pick(STYLES)().split(' ')[0];
  };
  const zed = (t) => t.replace(/s/g, (c) => (chance(0.6) ? 'z' : c)).replace(/S/g, (c) => (chance(0.6) ? 'Z' : c));
  return (taken = {}) => {
    for (let i = 0; i < 20; i++) {
      let n = chance(0.4) ? mix() : pick(STYLES)();
      const w = n.split(/\s+/).filter((x, j, a) => x && a.indexOf(x) === j).slice(0, 3); // no repeats, three words at most
      n = zed(w.join(' ')).slice(0, 40);
      if (n && !(n in taken)) return n;
    }
    return 'Preset ' + (Object.keys(taken).length + 1);
  };
})();

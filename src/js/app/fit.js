// ---------- fit the screen to the window (the Elixir Synth way) ----------
// On a desktop window the screen is a fixed 1000 x 647 pane (the shape of Johnny's MacBook Pro display, 1.545:1),
// scaled as a whole to fill the window with the dark case round it, so every page reads the same at any window size
// and nothing moves when you switch tabs. The scale goes on the root, so the Options sheet scales with the screen.
// Phones and short windows keep their own compact layout.
(() => {
  const main = document.querySelector('.main'), root = document.documentElement, W = 1000, H = 647, PAD = 16;
  const mq = matchMedia('(min-width: 701px) and (min-height: 521px)');
  const fit = () => {
    root.classList.toggle('fit', mq.matches);
    if (!mq.matches) { root.style.removeProperty('--k'); return; }
    const k = Math.min((innerWidth - PAD) / W, (innerHeight - PAD) / H, 2.2);
    root.style.setProperty('--k', k.toFixed(4));
  };
  addEventListener('resize', fit); mq.addEventListener('change', fit); fit();
})();

// ---------- slider fill ----------
// Each range slider's segmented bar is lit up to --f (its value as 0..1); programmatic value changes don't fire events, so it is re-read often.
(() => {
  const paint = (el) => { const f = (el.value - el.min) / ((el.max - el.min) || 1); const s = f.toFixed(4); if (el.dataset.f !== s) { el.dataset.f = s; el.style.setProperty('--f', s); } };
  const all = () => document.querySelectorAll('input[type=range]').forEach(paint);
  addEventListener('input', (e) => { if (e.target.type === 'range') paint(e.target); }, true);
  all(); setInterval(all, 120);
})();

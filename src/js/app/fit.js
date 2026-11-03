// ---------- fit the screen to the window (the Elixir Synth way) ----------
// On a desktop window the screen is laid out once at 1000px wide and scaled as a whole to fit, with the dark case round it,
// so it reads the same at any window size. Phones and short windows keep their own compact layout.
(() => {
  const main = document.querySelector('.main'), root = document.documentElement, W = 1000, PAD = 28;
  const mq = matchMedia('(min-width: 701px) and (min-height: 521px)');
  const fit = () => {
    root.classList.toggle('fit', mq.matches);
    if (!mq.matches) { main.style.removeProperty('--k'); return; }
    const k = Math.min((innerWidth - PAD) / W, (innerHeight - PAD) / main.offsetHeight, 2.2);
    main.style.setProperty('--k', k.toFixed(4));
  };
  new ResizeObserver(fit).observe(main);
  addEventListener('resize', fit); mq.addEventListener('change', fit); fit();
})();

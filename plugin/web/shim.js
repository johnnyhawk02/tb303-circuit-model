// Runs first on the plug-in's page (build.js puts it at the top of plugin/Resources/ui.html; the plug-in puts
// window.TB303_BOOT = { store } just before it). It gives the page:
//  - localStorage held by the plug-in: the keys the page starts with come from TB303_BOOT.store, and every write goes
//    back to the plug-in, which keeps this project's keys (session, banks, ...) with the DAW project and the rest
//    (presets, favourites, look) in one file shared by every copy of the plug-in
//  - TB303_BRIDGE: send(message) to the C++ engine, on(type, fn) for what the engine sends back
(() => {
  window.TB303_PLUGIN = true; document.documentElement.classList.add('plugin');
  const boot = window.TB303_BOOT || {}, data = new Map(Object.entries(boot.store || {}));
  const queue = [], handlers = {};
  let backend = null;
  const ready = () => {
    if (backend) return true;
    const b = window.__JUCE__ && window.__JUCE__.backend; if (!b) return false;
    backend = b;
    b.addEventListener('tb303', (m) => { const f = handlers[m && m.type]; if (f) try { f(m); } catch (e) { console.error(e); } });
    while (queue.length) b.emitEvent('tb303', queue.shift());
    return true;
  };
  const post = (m) => { if (ready()) backend.emitEvent('tb303', m); else queue.push(m); };
  const wait = setInterval(() => { if (ready()) clearInterval(wait); }, 20);
  window.TB303_BRIDGE = { send: post, on: (type, fn) => { handlers[type] = fn; } };
  const mem = {
    getItem: (k) => (data.has(String(k)) ? data.get(String(k)) : null),
    setItem(k, v) { k = String(k); v = String(v); if (data.get(k) === v) return; data.set(k, v); post({ type: 'store', key: k, value: v }); },
    removeItem(k) { k = String(k); if (!data.has(k)) return; data.delete(k); post({ type: 'store', key: k, value: null }); },
    clear() { for (const k of [...data.keys()]) mem.removeItem(k); },
    key: (i) => [...data.keys()][i] ?? null,
    get length() { return data.size; },
  };
  try { Object.defineProperty(window, 'localStorage', { value: mem, configurable: true, enumerable: true }); } catch {}
  if (window.localStorage !== mem) { // could not replace it: mirror into the real one and report its writes
    try {
      localStorage.clear(); for (const [k, v] of data) localStorage.setItem(k, v);
      const S = Storage.prototype, set = S.setItem, rem = S.removeItem;
      S.setItem = function (k, v) { set.call(this, k, v); post({ type: 'store', key: String(k), value: String(v) }); };
      S.removeItem = function (k) { rem.call(this, k); post({ type: 'store', key: String(k), value: null }); };
    } catch {}
  }
})();

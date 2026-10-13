// Shared by every page: talking to the pumpkin's API, its name, live status and small formatting
// helpers. Plain JavaScript, no build step, no outside files (the pumpkin may have no internet).
'use strict';

const Pumpkin = (() => {
  const $ = (id) => document.getElementById(id);

  // GET or POST some JSON. Every POST carries "X-Pumpkin: 1": the pumpkin refuses POSTs without it,
  // which stops other websites sending it requests (cheap CSRF protection). Throws an Error with a
  // message worth showing; error.needsBoot is true if the BOOT button has to be pressed first.
  async function request(path, { method = 'GET', body, timeout = 10000 } = {}) {
    const init = { method, headers: {}, cache: 'no-store' };
    if (method !== 'GET') {
      init.headers['X-Pumpkin'] = '1';
      init.headers['Content-Type'] = 'application/json';
      init.body = JSON.stringify(body || {});
    }
    const controller = window.AbortController ? new AbortController() : null;
    const timer = controller ? setTimeout(() => controller.abort(), timeout) : 0;
    if (controller) init.signal = controller.signal;
    let response;
    try {
      response = await fetch(path, init);
    } catch (e) {
      const error = new Error("Can't reach the pumpkin. Is this phone on the same WiFi as it?");
      error.unreachable = true; // no reply at all (it may still have done it)
      throw error;
    } finally {
      clearTimeout(timer);
    }
    let data = {};
    try { data = await response.json(); } catch (e) { /* not JSON */ }
    if (!response.ok) throw replyError(response.status, data);
    return data;
  }

  // An Error for a refused request ({"error":"...","needsBoot":true,"field":"name"} from the pumpkin)
  function replyError(status, data) {
    const error = new Error((data && data.error) || 'The pumpkin said no (' + status + ')');
    error.status = status;
    error.needsBoot = !!(data && data.needsBoot);
    error.field = (data && data.field) || '';
    return error;
  }
  const get = (path, options) => request(path, options);
  const post = (path, body, options) => request(path, Object.assign({ method: 'POST', body }, options));

  // Put the pumpkin's name in the title and in every [data-name] element
  function showName(name) {
    if (!name) return;
    document.querySelectorAll('[data-name]').forEach((el) => { el.textContent = name; });
    document.title = name + (document.body.dataset.title || '');
  }

  async function loadStatus() {
    const status = await get('/api/status');
    showName(status.name);
    return status;
  }

  // Why the pumpkin is on its own WiFi although home WiFi is saved. The firmware always says, so the
  // fallback is just in case.
  function setupReason(status) {
    return status.setupReason || 'The pumpkin couldn’t use ' + status.homeSsid + '.';
  }

  // Live news from the pumpkin: handlers.status(playerStatus), handlers.say(words). If server-sent
  // events don't work (some captive-portal browsers), ask for the status every few seconds instead.
  function listen(handlers) {
    let source = null;
    let pollTimer = 0;
    const poll = async () => {
      try {
        const status = await loadStatus();
        if (handlers.status) handlers.status(status.player);
        // The pumpkin's back but the browser gave up on events: start them again
        if (source && source.readyState === 2) {
          source.close();
          connect();
        }
      } catch (e) { /* try again next time */ }
    };
    const startPolling = () => { if (!pollTimer) { pollTimer = setInterval(poll, 4000); poll(); } };
    const stopPolling = () => { clearInterval(pollTimer); pollTimer = 0; };
    const connect = () => {
      if (!window.EventSource) { startPolling(); return; }
      source = new EventSource('/events');
      source.addEventListener('open', stopPolling);
      source.addEventListener('error', startPolling);
      source.addEventListener('status', (e) => {
        try { if (handlers.status) handlers.status(JSON.parse(e.data)); } catch (err) { /* ignore */ }
      });
      source.addEventListener('say', (e) => { if (handlers.say) handlers.say(e.data); });
    };
    // Out of sight (another tab, screen off): let go, so a forgotten tab doesn't keep the pumpkin's WiFi
    // on all night. Looked at again: listen again.
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') {
        if (source) source.close();
        stopPolling();
      } else if (!source || source.readyState === 2) {
        if (source) source.close();
        connect();
      }
    });
    connect();
  }

  // After a restart, keep asking until the pumpkin answers again. Calls done(status) or done(null).
  function waitForRestart(done, { firstWait = 5000, giveUpAfter = 90000 } = {}) {
    const start = Date.now();
    const attempt = async () => {
      try {
        done(await get('/api/status', { timeout: 3000 }));
      } catch (e) {
        if (Date.now() - start > giveUpAfter) done(null);
        else setTimeout(attempt, 2000);
      }
    };
    setTimeout(attempt, firstWait);
  }

  // Signal strength in words and bars (words from Scribe Evolution's wifi-utils.js)
  function signalWords(rssi) {
    if (rssi > -30) return 'Excellent';
    if (rssi > -50) return 'Very good';
    if (rssi > -60) return 'Good';
    if (rssi > -70) return 'Fair';
    return 'Poor';
  }
  function signalBars(rssi) {
    const level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    const bars = document.createElement('span');
    bars.className = 'bars s' + level;
    bars.setAttribute('role', 'img');
    bars.setAttribute('aria-label', signalWords(rssi) + ' signal');
    for (let i = 0; i < 4; i++) bars.appendChild(document.createElement('i'));
    return bars;
  }

  function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    if (days) return days + (days === 1 ? ' day ' : ' days ') + hours + ' h';
    if (hours) return hours + ' h ' + minutes + ' min';
    return minutes + (minutes === 1 ? ' minute' : ' minutes');
  }

  // ---- Effects and the player (the button page and the show creator) ----

  // Show mode (the saved show, round and round) or buttons mode (the show pauses, each press plays one)
  const setMode = (mode) => post('/api/mode', { mode });

  // "💥 Creeper" for an effect from /api/effects
  const effectName = (fx) => fx.emoji + ' ' + fx.label;

  // Effect tiles in sections (Pictures, Minecraft...), in the order the firmware lists them, as an
  // accordion: one section open at a time, the last one opened remembered under storeKey (the first
  // section to begin with). Each header shows the section's first emoji, its name and how many effects
  // it has; each section has its colour (the g-minecraft etc. classes in style.css). Each tile is a
  // <button data-id="creeper">: clicks are up to the page. titles: group -> heading (GET /api/effects).
  function effectAccordion(box, effects, { headingTag = 'h2', storeKey = 'section', titles = {} } = {}) {
    const groups = new Map(); // section name -> effects
    for (const fx of effects) {
      if (!groups.has(fx.group)) groups.set(fx.group, []);
      groups.get(fx.group).push(fx);
    }
    box.textContent = '';
    const sections = [];
    for (const [group, list] of groups) {
      const id = box.id + '-' + groupClass(group);
      const section = document.createElement('section');
      section.className = 'acc ' + groupClass(group);
      const heading = document.createElement(headingTag);
      heading.className = 'acc-h';
      const header = document.createElement('button');
      header.type = 'button';
      header.className = 'acc-btn';
      header.id = id + '-h';
      header.setAttribute('aria-controls', id);
      header.innerHTML = '<span class="e" aria-hidden="true"></span><span class="acc-name"></span> ' +
        '<span class="acc-count"></span><span class="chev" aria-hidden="true">▾</span>';
      header.querySelector('.e').textContent = list[0].emoji;
      // "Minecraft (Eyes Edition)": the bit in brackets goes smaller, on a line of its own
      const [, title, aside] = /^(.*?)(?:\s*(\(.*\)))?$/.exec(titles[group] || group);
      const name = header.querySelector('.acc-name');
      name.textContent = title;
      if (aside) {
        const small = document.createElement('small');
        small.className = 'acc-aside';
        small.textContent = aside;
        name.append(' ', small);
      }
      header.querySelector('.acc-count').innerHTML = list.length + '<span class="sr-only"> effects</span>';
      heading.appendChild(header);
      const grid = document.createElement('div');
      grid.className = 'grid';
      grid.id = id;
      grid.setAttribute('role', 'region');
      grid.setAttribute('aria-labelledby', header.id);
      for (const fx of list) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'tile';
        button.dataset.id = fx.id;
        const emoji = document.createElement('span');
        emoji.className = 'e';
        emoji.setAttribute('aria-hidden', 'true');
        emoji.textContent = fx.emoji;
        button.append(emoji, fx.label);
        grid.appendChild(button);
      }
      section.append(heading, grid);
      box.appendChild(section);
      sections.push({ group, header, grid });
    }
    const open = (wanted) => {
      for (const s of sections) {
        s.header.setAttribute('aria-expanded', String(s.group === wanted));
        s.grid.hidden = s.group !== wanted;
      }
    };
    const remembered = store.get(storeKey);
    open(sections.some((s) => s.group === remembered) ? remembered : sections.length ? sections[0].group : null);
    for (const s of sections) {
      s.header.addEventListener('click', () => {
        const opening = s.grid.hidden;
        open(opening ? s.group : null); // tapping the open one closes it
        if (!opening) return;
        store.set(storeKey, s.group);
        s.header.scrollIntoView({ block: 'start', behavior: reducedMotion() ? 'auto' : 'smooth' });
      });
    }
  }
  const groupClass = (group) => 'g-' + String(group).toLowerCase().replace(/[^a-z0-9]+/g, '-');
  const reducedMotion = () => !!(window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches);

  // "Couldn't get the buttons. <why> [Try again]" in place of something that didn't load
  function loadFailed(box, what, error, retry) {
    box.textContent = '';
    const p = document.createElement('p');
    p.className = 'center error';
    p.textContent = 'Couldn’t get ' + what + '. ' + error.message + ' ';
    const button = document.createElement('button');
    button.className = 'btn small secondary';
    button.type = 'button';
    button.textContent = 'Try again';
    button.addEventListener('click', retry);
    p.appendChild(button);
    box.appendChild(p);
  }

  // The status line: what the pumpkin is doing. player = {mode, effectId, label, block} from
  // /api/status or the "status" event; effects = Map of id -> effect.
  function playerText(player, effects) {
    const fx = effects.get(player.effectId);
    const label = player.label || '';
    const now = player.effectId === 'words' ? '“' + (label.length > 50 ? label.slice(0, 49) + '…' : label) + '”'
      : fx ? effectName(fx) : label || player.effectId || '';
    if (player.mode === 'show') return '🎃 The show is playing' + (now ? ': ' + now : '');
    if (player.effectId === 'words') return 'Scrolling: ' + now;
    if (now) return 'Playing: ' + now + ' …';
    return '⏸️ Buttons mode: the show is paused';
  }

  // iPhones type curly quotes; the pumpkin's font has straight ones
  const straightQuotes = (text) => text.replace(/[‘’]/g, "'").replace(/[“”]/g, '"');

  // localStorage can throw (private browsing, blocked storage), so wrap it
  const store = {
    get(key) { try { return localStorage.getItem('pumpkin.' + key); } catch (e) { return null; } },
    set(key, value) { try { localStorage.setItem('pumpkin.' + key, value); } catch (e) { /* not saved */ } },
  };

  // kind: 'ok', 'error', 'boot' (an error that needs the BOOT button pressed: made to stand out), or none
  function showMessage(el, text, kind) {
    el.textContent = text || '';
    el.classList.toggle('bad', kind === 'error' || kind === 'boot');
    el.classList.toggle('boot', kind === 'boot');
    el.classList.toggle('ok', kind === 'ok');
  }

  // Show an Error from request()
  function showError(el, error) {
    showMessage(el, error.message, error.needsBoot ? 'boot' : 'error');
  }

  return {
    $, get, post, replyError, loadStatus, setupReason, showName, listen, waitForRestart, signalWords, signalBars,
    formatUptime, setMode, effectName, effectAccordion, groupClass, reducedMotion, loadFailed, playerText, straightQuotes, store,
    showMessage, showError,
  };
})();

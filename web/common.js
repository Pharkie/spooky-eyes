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
      throw new Error("Can't reach the pumpkin. Is this phone on the same WiFi as it?");
    } finally {
      clearTimeout(timer);
    }
    let data = {};
    try { data = await response.json(); } catch (e) { /* not JSON */ }
    if (!response.ok) throw replyError(response.status, data);
    return data;
  }

  // An Error for a refused request ({"error":"...","needsBoot":true} from the pumpkin)
  function replyError(status, data) {
    const error = new Error((data && data.error) || 'The pumpkin said no (' + status + ')');
    error.needsBoot = !!(data && data.needsBoot);
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
    // Phones drop connections while the screen is off: reconnect when the page is looked at again
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible' && source && source.readyState === 2) {
        source.close();
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

  return { $, get, post, replyError, loadStatus, setupReason, showName, listen, waitForRestart, signalWords, signalBars, formatUptime, straightQuotes, store, showMessage, showError };
})();

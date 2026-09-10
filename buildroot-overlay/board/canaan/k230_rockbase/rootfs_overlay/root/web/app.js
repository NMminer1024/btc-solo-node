(function () {
  var mock = {
    system: {
      cpuPct: null,
      cpuLoad: "--",
      loadAverage: "--",
      diskPct: null,
      diskUsage: "--",
      memPct: null,
      memUsage: "--",
      swapPct: null,
      swapUsage: "--",
      cpuModel: "--",
      temperature: "--",
      rootfsUsage: "--",
      uptime: "--"
    },
    bitcoin: {
      syncBadge: "...",
      syncDetail: "...",
      ibdStageKey: "",
      ibdStageHeight: "",
      pruneBadge: "...",
      progressPct: null,
      blocks: "-- / --",
      meta: "Loading live node data...",
      connections: "--",
      difficulty: "--",
      rss: "--",
      version: "--",
      uptime: "--"
    },
    stratum: {
      badge: "...",
      badgeClass: "status-soft",
      url: "--",
      user: "wallet.workername",
      pass: "(leave blank)",
      mode: "LAN miners -> local Stratum"
    },
    network: {
      ip: "--",
      ssid: "--",
      mac: "--",
      rssi: "--",
      signalLevel: 0
    },
    wifi: {
      statusBadge: "Idle",
      statusClass: "status-soft",
      savedSsid: "-",
      connectedSsid: "-",
      connectedIp: "-",
      applyState: "idle",
      applyMessage: "",
      scanSupported: false,
      scanResults: []
    },
    about: {
      firmware: "--",
      updated: "--"
    }
  };

  var labels = {
    sync: {
      syncing: "IBD Syncing",
      pre_headers: "Pre-sync blockheaders",
      sync_headers: "Synchronizing blockheaders",
      sync_blocks: "Synchronizing blocks",
      loading_index: "Loading block index",
      loading_blocks: "Loading block files",
      verifying_blocks: "Verifying blocks",
      rewinding_blocks: "Rewinding blocks",
      pruning: "Pruning blocks",
      synced: "Synced",
      btc_not_running: "Not Running"
    },
    prune: {
      pruned_mode: "Pruned Node",
      full_mode: "Full Node"
    },
    stratum: {
      running: "Running",
      stopped: "Stopped"
    }
  };

  var validViews = {
    dashboard: "Dashboard",
    miners: "Miners",
    miner: "Miner",
    logs: "Logs",
    settings: "Settings",
    about: "About"
  };

  var viewAliases = {
    node: "settings",
    stratum: "settings",
    network: "settings"
  };

  var themes = {
    light: {
      label: "Dark",
      icon: "◐"
    },
    dark: {
      label: "Light",
      icon: "◑"
    }
  };

  var hasLoadedDashboardData = false;
  var DASHBOARD_REFRESH_SECONDS = 30;
  var countdownTimer = null;
  var countdownDeadline = 0;
  var refreshInFlight = false;
  var refreshSecondsLeft = DASHBOARD_REFRESH_SECONDS;
  var LOG_REFRESH_SECONDS = 5;
  var LOG_TAIL_LIMIT = 300;
  var LOG_MAX_CHARS = 262144;
  var MINERS_REFRESH_SECONDS = 60;
  var logPollTimer = null;
  var minersPollTimer = null;
  var minersLoading = false;
  var minersLoaded = false;
  var minerWorkerSort = {};
  var nodeHashrateVisible = {
    h1h: true,
    h1d: true,
    h7d: true
  };
  var activeViewName = "dashboard";
  var activeLogService = "bitcoind";
  var logRequestSeq = {
    bitcoind: 0,
    stratum: 0
  };
  var wifiRequestSeq = 0;
  var wifiState = {
    loaded: false,
    loading: false,
    scanning: false,
    applying: false,
    current: mock.wifi,
    scanResults: [],
    selectedSsid: "",
    scanCacheLoaded: false
  };
  var wifiPollTimer = null;
  var logState = {
    bitcoind: {
      cursor: 0,
      text: "",
      loaded: false,
      loading: false,
      path: "/root/block/debug.log",
      label: "Bitcoind log",
      summary: "Tail 300 lines · live cursor"
    },
    stratum: {
      cursor: 0,
      text: "",
      loaded: false,
      loading: false,
      path: "/root/ckpool/logs/ckpool.log",
      label: "Stratum log",
      summary: "Tail 300 lines · live cursor"
    }
  };

  function setText(id, value) {
    var el = document.getElementById(id);
    if (el) el.textContent = value;
  }

  function getLogState(service) {
    return logState[service] || logState.bitcoind;
  }

  function setLogSummary(service, text) {
    var state = getLogState(service);
    state.summary = text;
    if (activeLogService === service) setText("logSummary", text);
  }

  function trimLogText(text) {
    var value = typeof text === "string" ? text : "";
    if (value.length <= LOG_MAX_CHARS) return value;
    return value.slice(value.length - LOG_MAX_CHARS);
  }

  function renderLogView(service) {
    var state = getLogState(service);
    if (activeViewName !== "logs" || activeLogService !== service) return;
    setText("logServiceBadge", state.label);
    setText("logPath", state.path);
    setText("logSummary", state.summary);
    setText("logsPanel", state.text || "[empty]");
    var panel = document.getElementById("logsPanel");
    if (panel) panel.scrollTop = panel.scrollHeight;
  }

  function applyLogPayload(service, payload, initialRequest) {
    var state = getLogState(service);
    var incoming = payload && typeof payload.text === "string" ? payload.text : "";
    var reset = payload && payload.reset === true;
    var initial = initialRequest || (payload && payload.initial === true);
    var summary = initial ? "Tail 300 lines · live cursor" : "Polling live updates every 5s";

    if (reset || initial || !state.loaded) {
      state.text = trimLogText(incoming);
    } else if (incoming) {
      state.text = trimLogText(state.text + incoming);
    }

    state.cursor = toNumber(payload && payload.cursor, state.cursor);
    state.loaded = true;
    state.path = toText(payload && payload.path, state.path);
    state.label = (payload && payload.service === "stratum") ? "Stratum log" : "Bitcoind log";
    state.loading = false;
    setLogSummary(service, reset || (payload && payload.truncated) ? "Log rotated or truncated · reloaded tail" : summary);
    renderLogView(service);
  }

  function stopLogPolling() {
    if (logPollTimer) {
      window.clearInterval(logPollTimer);
      logPollTimer = null;
    }
  }

  function fetchLogData(service, initialRequest) {
    var state = getLogState(service);
    if (state.loading) return Promise.resolve(null);

    state.loading = true;
    var seq = (logRequestSeq[service] || 0) + 1;
    logRequestSeq[service] = seq;
    var cursor = initialRequest ? 0 : state.cursor;
    var url = "/api/log.cgi?service=" + encodeURIComponent(service) +
      "&cursor=" + encodeURIComponent(cursor || 0) +
      "&limit=" + LOG_TAIL_LIMIT +
      "&_ts=" + Date.now();

    return window.fetch(url, { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      })
      .then(function (payload) {
        if (logRequestSeq[service] !== seq) return null;
        applyLogPayload(service, payload, initialRequest);
        return payload;
      })
      .catch(function () {
        if (logRequestSeq[service] !== seq) return null;
        state.loading = false;
        if (!state.loaded) {
          state.text = "Log unavailable.";
          renderLogView(service);
        }
        return null;
      })
      .then(function (result) {
        state.loading = false;
        return result;
      });
  }

  function startLogPolling(service) {
    stopLogPolling();
    activeLogService = service;
    setLogSummary(service, getLogState(service).loaded ? "Polling live updates every 5s" : "Tail 300 lines · live cursor");
    renderLogView(service);
    if (!getLogState(service).loaded) {
      fetchLogData(service, true);
    } else {
      fetchLogData(service, false);
    }
    logPollTimer = window.setInterval(function () {
      if (activeLogService !== service) return;
      fetchLogData(service, false);
    }, LOG_REFRESH_SECONDS * 1000);
  }

  function bindLogTabs() {
    var tabs = document.querySelectorAll("[data-log-service]");
    tabs.forEach(function (tab) {
      tab.addEventListener("click", function () {
        var service = tab.getAttribute("data-log-service") || "bitcoind";
        tabs.forEach(function (item) {
          item.classList.toggle("is-active", item.getAttribute("data-log-service") === service);
        });
        startLogPolling(service);
      });
    });
  }

  function setRefreshCountdown(secondsLeft) {
    var value = Number(secondsLeft);
    var safeSeconds = Number.isFinite(value) ? Math.max(0, value) : DASHBOARD_REFRESH_SECONDS;
    var orb = document.getElementById("bitcoinRefreshOrb");
    var label = document.getElementById("bitcoinRefreshCountdown");
    if (orb) orb.style.setProperty("--pct", safeSeconds * 100 / DASHBOARD_REFRESH_SECONDS);
    if (label) label.textContent = Math.ceil(safeSeconds) + "s";
  }

  function clearRefreshCountdownTimer() {
    if (!countdownTimer) return;
    window.cancelAnimationFrame(countdownTimer);
    countdownTimer = null;
  }

  function completeRefreshCycle() {
    refreshInFlight = false;
    refreshSecondsLeft = DASHBOARD_REFRESH_SECONDS;
    setRefreshCountdown(refreshSecondsLeft);
    countdownDeadline = window.performance.now() + DASHBOARD_REFRESH_SECONDS * 1000;
  }

  function startRefreshCountdown() {
    clearRefreshCountdownTimer();
    refreshSecondsLeft = DASHBOARD_REFRESH_SECONDS;
    countdownDeadline = window.performance.now() + DASHBOARD_REFRESH_SECONDS * 1000;
    setRefreshCountdown(refreshSecondsLeft);
    var tick = function (now) {
      if (!countdownDeadline) {
        countdownTimer = window.requestAnimationFrame(tick);
        return;
      }

      var remainingSeconds = Math.max(0, (countdownDeadline - now) / 1000);
      refreshSecondsLeft = remainingSeconds;
      setRefreshCountdown(remainingSeconds);

      if (!refreshInFlight && remainingSeconds <= 0) {
        fetchDashboardData();
      }

      countdownTimer = window.requestAnimationFrame(tick);
    };
    countdownTimer = window.requestAnimationFrame(tick);
  }

  function isRollDigit(char) {
    return /^[0-9]$/.test(char);
  }

  function clearRollCleanup(el) {
    if (!el || !el._rollCleanupTimer) return;
    window.clearTimeout(el._rollCleanupTimer);
    el._rollCleanupTimer = null;
  }

  function buildRollingChars(el, prevValue, nextValue, shouldAnimate) {
    var prevChars = String(prevValue || "").split("");
    var nextChars = String(nextValue || "").split("");
    var maxLen = Math.max(prevChars.length, nextChars.length);
    var frag = document.createDocumentFragment();
    var animatedCount = 0;

    while (prevChars.length < maxLen) prevChars.unshift("");
    while (nextChars.length < maxLen) nextChars.unshift("");

    nextChars.forEach(function (char, index) {
      var prevChar = prevChars[index];
      var slot = document.createElement("span");
      slot.className = "roll-char";

      if (char === " ") {
        slot.classList.add("roll-char-space");
        slot.textContent = " ";
        frag.appendChild(slot);
        return;
      }

      var canAnimate = shouldAnimate && isRollDigit(prevChar) && isRollDigit(char) && prevChar !== char;
      if (canAnimate) {
        var clip = document.createElement("span");
        var track = document.createElement("span");
        var oldFace = document.createElement("span");
        var newFace = document.createElement("span");

        slot.classList.add("roll-char-clip", "is-pending");
        clip.className = "roll-clip";
        track.className = "roll-track";
        oldFace.className = "roll-face";
        newFace.className = "roll-face";
        oldFace.textContent = prevChar;
        newFace.textContent = char;
        track.appendChild(oldFace);
        track.appendChild(newFace);
        clip.appendChild(track);
        slot.appendChild(clip);
        animatedCount += 1;
      } else {
        slot.textContent = char || "";
        if (isRollDigit(char)) slot.classList.add("roll-char-digit");
        if (char === ",") slot.classList.add("roll-char-comma");
        if (char === ".") slot.classList.add("roll-char-dot");
        if (char === "%") slot.classList.add("roll-char-percent");
      }

      frag.appendChild(slot);
    });

    el.replaceChildren(frag);
    el.setAttribute("aria-label", nextValue);

    if (animatedCount) {
      window.requestAnimationFrame(function () {
        el.querySelectorAll(".roll-char.is-pending").forEach(function (slot, index) {
          window.setTimeout(function () {
            slot.classList.add("is-running");
          }, index * 38);
        });
      });
    }
  }

  function setRollingValue(id, value) {
    var el = document.getElementById(id);
    if (!el) return;

    var nextValue = toText(value, "--");
    var prevValue = el.dataset.rollValue || "";
    var isInitial = !prevValue;
    var shouldAnimate = !isInitial && prevValue !== nextValue && /\d/.test(prevValue) && /\d/.test(nextValue) && nextValue.indexOf("--") === -1;

    clearRollCleanup(el);
    buildRollingChars(el, prevValue || nextValue, nextValue, shouldAnimate);
    el.dataset.rollValue = nextValue;

    if (shouldAnimate) {
      el._rollCleanupTimer = window.setTimeout(function () {
        buildRollingChars(el, nextValue, nextValue, false);
        el._rollCleanupTimer = null;
      }, 760);
    }
  }

  function setIbdMeta(id, chainName, blockDate, stageKey) {
    var el = document.getElementById(id);
    if (!el) return;

    var chainText = toText(chainName, "");
    var blockText = toText(blockDate, "");
    var isIbdStage = stageKey === "pre_headers" || stageKey === "sync_headers" || stageKey === "sync_blocks" || stageKey === "syncing";
    var hasDate = blockText && blockText !== "-" && blockText !== "N/A";
    var hasChain = chainText && chainText !== "-" && chainText !== "N/A";

    if (!hasChain && !hasDate) {
      el.textContent = mock.bitcoin.meta;
      return;
    }

    if (isIbdStage) {
      if (hasChain) {
        el.innerHTML = '<span class="ibd-meta-chain">' + chainText + "</span>";
        return;
      }
      if (hasDate) {
        el.innerHTML = '<span class="ibd-meta-date">' + blockText + "</span>";
        return;
      }
    }

    if (!hasDate) {
      el.textContent = chainText;
      return;
    }

    var match = blockText.match(/^(\d{4}-\d{2}-\d{2})\s+(.+)$/);
    var html = "";
    if (hasChain) html += '<span class="ibd-meta-chain">' + chainText + "</span>";
    if (hasChain) html += '<span class="ibd-meta-sep"> · </span>';
    if (match) {
      html += '<span class="ibd-meta-date">' + match[1] + "</span>";
      html += ' <span class="ibd-meta-time">' + match[2] + "</span>";
    } else {
      html += '<span class="ibd-meta-date">' + blockText + "</span>";
    }
    el.innerHTML = html;
  }

  function setIbdStage(id, stageKey, stageText, blockDate) {
    var el = document.getElementById(id);
    if (!el) return;

    var text = toText(stageText, "");
    var dateText = toText(blockDate, "");
    var isDateStage = stageKey === "syncing" || stageKey === "sync_headers" || stageKey === "sync_blocks";
    var match = dateText.match(/^(\d{4}-\d{2}-\d{2})\s+(.+)$/);

    if (isDateStage && match) {
      el.innerHTML = '<span class="ibd-stage-date">' + match[1] + '</span>' +
        ' <span class="ibd-stage-time">' + match[2] + '</span>';
      return;
    }

    el.textContent = text || dateText || mock.bitcoin.syncDetail;
  }

  function setProgress(id, pct) {
    var el = document.getElementById(id);
    if (!el) return;
    var safePct = Number.isFinite(Number(pct)) ? Number(pct) : 0;
    el.style.setProperty("--pct", safePct);
  }

  function setBadgeState(id, text, statusClass) {
    var el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.classList.remove("status-ok", "status-soft", "status-warm", "status-danger");
    if (statusClass) el.classList.add(statusClass);
  }

  function toNumber(value, fallback) {
    var num = Number(value);
    return Number.isFinite(num) ? num : fallback;
  }

  function toText(value, fallback) {
    return value === undefined || value === null || value === "" ? fallback : String(value);
  }

  function formatPercent(value, digits) {
    var num = Number(value);
    if (!Number.isFinite(num)) return "--";
    return num.toFixed(digits) + "%";
  }

  function formatPercentSig(value, digits) {
    var num = Number(value);
    if (!Number.isFinite(num)) return "--";
    return num.toPrecision(digits) + "%";
  }

  function formatIbdPercent(value) {
    var num = Number(value);
    if (!Number.isFinite(num)) return "--";
    if (num >= 100) return "100%";
    if (num < 1) return num.toPrecision(2) + "%";
    return (Math.floor(num * 100) / 100).toFixed(2) + "%";
  }

  function joinParts(parts, fallback) {
    var clean = parts.filter(function (part) {
      return part && part !== "-" && part !== "N/A";
    });
    return clean.length ? clean.join(" · ") : fallback;
  }

  function parseRssiDbm(value) {
    var match = String(value || "").match(/-?\d+/);
    return match ? Number(match[0]) : NaN;
  }

  function mapStatusClass(value, fallback) {
    var map = {
      ok: "status-ok",
      soft: "status-soft",
      warn: "status-warm",
      err: "status-danger"
    };
    return map[value] || fallback;
  }

  function normalizeDashboardData(api) {
    if (!api || typeof api !== "object") return mock;

    var system = api.system || {};
    var bitcoin = api.bitcoin || {};
    var mining = api.mining || {};
    var network = api.network || {};
    var progressPct = toNumber(bitcoin.progress_ring, toNumber(bitcoin.progress_pct, mock.bitcoin.progressPct));
    var syncLabel = labels.sync[toText(bitcoin.sync_key, "")] || mock.bitcoin.syncBadge;
    var pruneLabel = labels.prune[toText(bitcoin.prune_key, "")] || mock.bitcoin.pruneBadge;
    var stratumLabel = labels.stratum[toText(mining.ckpool_state, "")] || mock.stratum.badge;
    var chainName = toText(bitcoin.btc_context, "").replace(/^main\b/, "Mainnet");
    var loadAverageFallback = joinParts([
      system.load_avg_1 && "1m " + system.load_avg_1,
      system.load_avg_5 && "5m " + system.load_avg_5
    ], mock.system.loadAverage);

    return {
      system: {
        cpuPct: toNumber(system.load_pct, mock.system.cpuPct),
        cpuLoad: toText(system.load_display, mock.system.cpuLoad),
        loadAverage: toText(system.load_avg_display, loadAverageFallback),
        diskPct: toNumber(system.disk_pct, mock.system.diskPct),
        diskUsage: toText(system.disk_size_info, mock.system.diskUsage),
        memPct: toNumber(system.mem_pct, mock.system.memPct),
        memUsage: joinParts([system.mem_used_display, system.mem_total_display && "/ " + system.mem_total_display], mock.system.memUsage).replace(" · / ", " / "),
        swapPct: toNumber(system.swap_pct, mock.system.swapPct),
        swapUsage: joinParts([system.swap_used_display, system.swap_total_display && "/ " + system.swap_total_display], mock.system.swapUsage).replace(" · / ", " / "),
        cpuModel: toText(system.cpu_model, mock.system.cpuModel),
        temperature: toText(system.temp, mock.system.temperature),
        rootfsUsage: toText(system.rootfs_size_info, mock.system.rootfsUsage),
        uptime: toText(system.uptime, mock.system.uptime)
      },
      bitcoin: {
        syncBadge: syncLabel,
        syncDetail: toText(bitcoin.sync_detail, mock.bitcoin.syncDetail),
        ibdStageKey: toText(bitcoin.ibd_stage_key, mock.bitcoin.ibdStageKey),
        ibdStageHeight: toText(bitcoin.ibd_stage_height, mock.bitcoin.ibdStageHeight),
        syncClass: mapStatusClass(toText(bitcoin.sync_class, ""), "status-ok"),
        pruneBadge: pruneLabel,
        pruneClass: mapStatusClass(toText(bitcoin.prune_class, ""), "status-soft"),
        progressPct: progressPct,
        blocks: joinParts([bitcoin.blocks_fmt, "/ " + toText(bitcoin.headers_fmt, "-")], mock.bitcoin.blocks).replace(" · / ", " / "),
        chainName: chainName,
        bestBlockDate: toText(bitcoin.best_block_date, "-"),
        meta: joinParts([chainName, bitcoin.best_block_date], mock.bitcoin.meta),
        connections: toText(bitcoin.connections, mock.bitcoin.connections),
        difficulty: toText(bitcoin.difficulty_display, mock.bitcoin.difficulty),
        rss: toText(bitcoin.rss, mock.bitcoin.rss),
        version: toText(bitcoin.version, mock.bitcoin.version),
        uptime: toText(system.uptime, mock.bitcoin.uptime)
      },
      stratum: {
        badge: stratumLabel,
        badgeClass: mapStatusClass(toText(mining.ckpool_class, ""), "status-ok"),
        url: toText(mining.stratum_url, mock.stratum.url),
        user: toText(mining.user_hint, mock.stratum.user),
        pass: toText(mining.pass_hint, mock.stratum.pass),
        mode: toText(mining.recommended_mode, mock.stratum.mode)
      },
      network: {
        ip: toText(network.ip_addr, mock.network.ip),
        ssid: toText(network.wifi_ssid, mock.network.ssid),
        mac: toText(network.mac_addr, mock.network.mac),
        rssi: toText(network.rssi_display, mock.network.rssi),
        signalLevel: toNumber(network.signal_level, getRssiLevel(toText(network.rssi_display, mock.network.rssi)))
      },
      about: mock.about
    };
  }

  function normalizeWifiData(api) {
    if (!api || typeof api !== "object") return mock.wifi;

    var scanResults = Array.isArray(api.scan_results) ? api.scan_results : [];
    var applyState = toText(api.apply_state, "idle");
    var currentState = toText(api.current_state, "");
    var statusBadge = "Idle";
    var statusClass = "status-soft";
    if (applyState === "applying") {
      statusBadge = "Applying";
      statusClass = "status-warm";
    } else if (currentState === "COMPLETED" || toText(api.current_ip, "-") !== "-") {
      statusBadge = "Connected";
      statusClass = "status-ok";
    } else if (applyState === "ready") {
      statusBadge = "Ready";
      statusClass = "status-ok";
    } else if (applyState === "error") {
      statusBadge = "Error";
      statusClass = "status-danger";
    }

    return {
      statusBadge: statusBadge,
      statusClass: statusClass,
      savedSsid: toText(api.saved_ssid, "-"),
      connectedSsid: toText(api.current_ssid, "-"),
      connectedIp: toText(api.current_ip, "-"),
      connectedState: toText(api.current_state, "-"),
      applyState: applyState,
      applyMessage: toText(api.apply_message, ""),
      scanSupported: !!api.scan_supported,
      scanResults: scanResults,
      iface: toText(api.iface, "wlan0")
    };
  }

  function stopMinersPolling() {
    if (minersPollTimer) {
      window.clearInterval(minersPollTimer);
      minersPollTimer = null;
    }
  }

  function hashrateToGhs(value) {
    var text = String(value || "0").trim();
    if (!text || text === "-") return 0;
    var suffix = text.slice(-1).toUpperCase();
    var number = parseFloat(text);
    var units = { H: 1e-9, K: 1e-6, M: 1e-3, G: 1, T: 1e3, P: 1e6, E: 1e9 };
    if (!Number.isFinite(number)) return 0;
    return number * (units[suffix] || 1);
  }

  function formatCount(value) {
    var num = Number(value);
    if (!Number.isFinite(num)) return "--";
    return Math.round(num).toLocaleString();
  }

  function formatShare(value) {
    var num = Number(value);
    if (!Number.isFinite(num)) return "--";
    var units = [
      { label: "T", value: 1e12 },
      { label: "G", value: 1e9 },
      { label: "M", value: 1e6 },
      { label: "K", value: 1e3 }
    ];
    for (var i = 0; i < units.length; i += 1) {
      if (num >= units[i].value) return (num / units[i].value).toFixed(2) + units[i].label;
    }
    return Math.round(num).toLocaleString();
  }

  function getWorkerSortState(address) {
    var key = toText(address, "default");
    if (!minerWorkerSort[key]) minerWorkerSort[key] = { key: "h5m", dir: "desc" };
    return minerWorkerSort[key];
  }

  function workerSortValue(worker, key) {
    if (key === "worker") return toText(worker.name || worker.full_name, "").toLowerCase();
    if (key === "h1m") return hashrateToGhs(worker.hashrate1m);
    if (key === "h5m") return hashrateToGhs(worker.hashrate5m);
    if (key === "h1h") return hashrateToGhs(worker.hashrate1hr);
    if (key === "h1d") return hashrateToGhs(worker.hashrate1d);
    if (key === "h7d") return hashrateToGhs(worker.hashrate7d);
    if (key === "bestshare") return Number(worker.bestshare) || 0;
    if (key === "bestever") return Number(worker.bestever) || 0;
    if (key === "lastseen") return Number(worker.lastshare) || 0;
    return 0;
  }

  function sortWorkers(workers, state) {
    var sortKey = state && state.key ? state.key : "h5m";
    var dir = state && state.dir === "asc" ? 1 : -1;
    return workers.slice().sort(function (a, b) {
      var av = workerSortValue(a, sortKey);
      var bv = workerSortValue(b, sortKey);
      if (typeof av === "string" || typeof bv === "string") {
        return String(av).localeCompare(String(bv)) * dir;
      }
      if (av === bv) return toText(a.name || a.full_name, "").localeCompare(toText(b.name || b.full_name, ""));
      return (av - bv) * dir;
    });
  }

  function formatApiAge(generatedAt) {
    var ts = Number(generatedAt);
    if (!Number.isFinite(ts) || ts <= 0) return "cache age unknown";
    var diff = Math.max(0, Math.floor(Date.now() / 1000 - ts));
    if (diff < 60) return "updated " + diff + "s ago";
    if (diff < 3600) return "updated " + Math.floor(diff / 60) + "m ago";
    return "updated " + Math.floor(diff / 3600) + "h ago";
  }

  function formatHashrateAxis(value) {
    var num = Number(value);
    if (!Number.isFinite(num) || num <= 0) return "0";
    var units = [
      { label: "EH", value: 1e9 },
      { label: "PH", value: 1e6 },
      { label: "TH", value: 1e3 },
      { label: "GH", value: 1 }
    ];
    for (var i = 0; i < units.length; i += 1) {
      if (num >= units[i].value) {
        var scaled = num / units[i].value;
        return (scaled >= 100 ? scaled.toFixed(0) : scaled >= 10 ? scaled.toFixed(1) : scaled.toFixed(2)) + units[i].label;
      }
    }
    return num.toFixed(1) + "GH";
  }

  function formatChartTime(ts) {
    var date = new Date(Number(ts) * 1000);
    if (Number.isNaN(date.getTime())) return "--";
    return (date.getMonth() + 1) + "/" + date.getDate() + " " +
      date.getHours().toString().padStart(2, "0") + ":" +
      date.getMinutes().toString().padStart(2, "0");
  }

  function bindMinersHistoryTooltip(chart, points, series, dims) {
    var tooltip = chart.querySelector(".miners-history-tooltip");
    var hoverLine = chart.querySelector(".miners-history-hover-line");
    if (!tooltip || !hoverLine || !points.length) return;
    var rectLeft = dims.left;
    var rectRight = dims.width - dims.right;
    var plotWidth = Math.max(1, rectRight - rectLeft);
    var span = Math.max(1, points.length - 1);

    chart.onmousemove = function (event) {
      var rect = chart.getBoundingClientRect();
      var ratio = Math.max(0, Math.min(1, (event.clientX - rect.left - rectLeft) / plotWidth));
      var index = Math.max(0, Math.min(points.length - 1, Math.round(ratio * span)));
      var point = points[index];
      var x = rectLeft + plotWidth * index / span;
      var rows = series.map(function (item) {
        return '<div><span><i style="background:' + item.color + '"></i>' + escapeHtml(item.label) + '</span><strong>' +
          escapeHtml(toText(point[item.key], "--")) + '</strong></div>';
      }).join("");
      tooltip.innerHTML = '<b>' + escapeHtml(formatChartTime(point.ts)) + '</b>' + rows;
      tooltip.style.opacity = "1";
      tooltip.style.left = Math.min(rect.width - 178, Math.max(8, x + 12)) + "px";
      tooltip.style.top = Math.max(8, event.clientY - rect.top - 18) + "px";
      hoverLine.setAttribute("x1", x.toFixed(1));
      hoverLine.setAttribute("x2", x.toFixed(1));
      hoverLine.style.opacity = "1";
    };

    chart.onmouseleave = function () {
      tooltip.style.opacity = "0";
      hoverLine.style.opacity = "0";
    };
  }

  function escapeHtml(value) {
    return String(value === undefined || value === null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  /* Catmull-Rom -> cubic bezier, k tuned to match Chart.js tension 0.3 */
  function smoothLinePath(coords) {
    if (coords.length < 2) return "";
    var k = 0.15;
    var d = "M" + coords[0][0].toFixed(1) + " " + coords[0][1].toFixed(1);
    for (var i = 0; i < coords.length - 1; i += 1) {
      var p0 = coords[i - 1] || coords[i];
      var p1 = coords[i];
      var p2 = coords[i + 1];
      var p3 = coords[i + 2] || p2;
      var c1x = p1[0] + (p2[0] - p0[0]) * k;
      var c1y = p1[1] + (p2[1] - p0[1]) * k;
      var c2x = p2[0] - (p3[0] - p1[0]) * k;
      var c2y = p2[1] - (p3[1] - p1[1]) * k;
      d += "C" + c1x.toFixed(1) + " " + c1y.toFixed(1) + " " +
        c2x.toFixed(1) + " " + c2y.toFixed(1) + " " +
        p2[0].toFixed(1) + " " + p2[1].toFixed(1);
    }
    return d;
  }

  /* 'before'-style stepped path (worker count charts) */
  function steppedPath(coords) {
    if (!coords.length) return "";
    var d = "M" + coords[0][0].toFixed(1) + " " + coords[0][1].toFixed(1);
    for (var i = 1; i < coords.length; i += 1) {
      d += "H" + coords[i][0].toFixed(1) + "V" + coords[i][1].toFixed(1);
    }
    return d;
  }

  /* Shared SVG chart renderer: smoothed lines, optional area fill, tooltip */
  function buildHistoryChart(chart, points, series, opts) {
    opts = opts || {};
    if (!chart) return;
    var height = opts.height || 330;
    var formatAxis = opts.formatAxis || formatHashrateAxis;
    var max = points.reduce(function (acc, row) {
      return series.reduce(function (inner, item) {
        return Math.max(inner, hashrateToGhs(row[item.key]));
      }, acc);
    }, 0);
    chart.style.minHeight = height + "px";
    if (max <= 0 || points.length < 2 || !series.length) {
      chart.innerHTML = opts.emptyHtml || "";
      return;
    }

    var width = Math.max(520, Math.round(chart.clientWidth || 0));
    var left = 70;
    var right = 18;
    var top = 24;
    var bottom = 46;
    var span = Math.max(1, points.length - 1);
    var yTicks = [0, 0.25, 0.5, 0.75, 1].map(function (ratio) {
      var y = height - bottom - ratio * (height - top - bottom);
      var value = max * ratio;
      return '<path class="miners-history-grid" d="M' + left + ' ' + y.toFixed(1) + 'H' + (width - right) + '"></path>' +
        '<text class="miners-history-y-label" x="' + (left - 10) + '" y="' + (y + 4).toFixed(1) + '" text-anchor="end">' + escapeHtml(formatAxis(value)) + '</text>';
    }).join("");
    var labels = points.map(function (row, index) {
      if (index !== 0 && index !== points.length - 1 && index % Math.max(1, Math.floor(points.length / 3)) !== 0) return "";
      var x = left + (width - left - right) * index / span;
      var text = formatChartTime(row.ts);
      return '<text x="' + x.toFixed(1) + '" y="' + (height - 9) + '" text-anchor="middle">' + escapeHtml(text) + '</text>';
    }).join("");
    var lines = series.map(function (item) {
      var coords = points.map(function (row, index) {
        var x = left + (width - left - right) * index / span;
        var value = hashrateToGhs(row[item.key]);
        var y = height - bottom - (value / max) * (height - top - bottom);
        return [x, y];
      });
      if (!coords.length) return "";
      var d = opts.stepped ? steppedPath(coords) : smoothLinePath(coords);
      var area = "";
      if (item.fill) {
        area = '<path class="miners-history-area" style="fill:' + item.fill + '" d="' + d +
          "L" + coords[coords.length - 1][0].toFixed(1) + " " + (height - bottom).toFixed(1) +
          "L" + coords[0][0].toFixed(1) + " " + (height - bottom).toFixed(1) + 'Z"></path>';
      }
      return area + '<path class="miners-history-line" style="stroke:' + item.color + '" d="' + d + '"></path>';
    }).join("");

    chart.innerHTML = '<svg class="miners-history-svg" style="height:' + height + 'px" viewBox="0 0 ' + width + ' ' + height + '" role="img" aria-label="' + escapeHtml(opts.ariaLabel || "Hashrate history") + '">' +
      yTicks +
      lines +
      '<line class="miners-history-hover-line" x1="' + left + '" x2="' + left + '" y1="' + top + '" y2="' + (height - bottom) + '"></line>' +
      '<rect class="miners-history-hitbox" x="' + left + '" y="' + top + '" width="' + (width - left - right) + '" height="' + (height - top - bottom) + '"></rect>' +
      labels +
      '</svg>' +
      '<div class="miners-history-tooltip" aria-hidden="true"></div>';
    bindMinersHistoryTooltip(chart, points, series, {
      width: width,
      left: left,
      right: right
    });
  }

  /* Wallet-level history (pi5 user page alignment): 5m/1h/1d/7d, 5m filled */
  var WALLET_HISTORY_TTL_MS = 60000;
  var walletHistoryCache = {};
  var walletHistorySeries = [
    { key: "h5m", label: "5m", color: "#58a6ff", fill: "rgba(88,166,255,0.07)" },
    { key: "h1h", label: "1h", color: "#3fb950" },
    { key: "h1d", label: "1d", color: "#d29922" },
    { key: "h7d", label: "7d", color: "#f85149" }
  ];

  function formatCountAxis(value) {
    var num = Math.round(Number(value) || 0);
    return String(num);
  }

  function drawWalletHistory(address, payload) {
    if (address !== activeMinerAddress) return;
    var plot = document.getElementById("minerDetailHashChart");
    var legend = document.getElementById("minerDetailHashLegend");
    var points = payload && Array.isArray(payload.points) ? payload.points : [];
    buildHistoryChart(plot, points, walletHistorySeries, {
      height: 300,
      ariaLabel: "Wallet hashrate history",
      emptyHtml: '<div class="miners-empty">Collecting wallet history, check back in a few minutes...</div>'
    });
    if (legend) {
      legend.innerHTML = walletHistorySeries.map(function (item) {
        return '<span class="miners-history-key"><i style="background:' + item.color + '"></i>' + escapeHtml(item.label) + '</span>';
      }).join("");
    }
    buildHistoryChart(document.getElementById("minerDetailWorkerChart"), points, [
      { key: "workers", label: "Workers", color: "#d29922", fill: "rgba(210,153,34,0.1)" }
    ], {
      height: 160,
      stepped: true,
      formatAxis: formatCountAxis,
      ariaLabel: "Worker count history",
      emptyHtml: '<div class="miners-empty">Collecting worker history, check back in a few minutes...</div>'
    });
  }

  function fetchWalletHistory(address) {
    var cached = walletHistoryCache[address];
    if (cached && Date.now() - cached.fetchedAt < WALLET_HISTORY_TTL_MS) {
      drawWalletHistory(address, cached.payload);
      return;
    }
    window.fetch("/api/miners-history.cgi?address=" + encodeURIComponent(address) + "&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      })
      .then(function (payload) {
        walletHistoryCache[address] = { fetchedAt: Date.now(), payload: payload };
        drawWalletHistory(address, payload);
      })
      .catch(function () {});
  }

  function renderNodeHashrateHistory(pool, history) {
    var points = history && Array.isArray(history.points) ? history.points : [];

    var series = [
      { key: "h1h", label: "1h", color: "#2dd4bf", fill: "rgba(45,212,191,0.07)" },
      { key: "h1d", label: "1d", color: "#d29922" },
      { key: "h7d", label: "7d", color: "#f85149" }
    ];
    var visibleSeries = series.filter(function (item) {
      return nodeHashrateVisible[item.key] !== false;
    });
    var chart = document.getElementById("minerHistoryChart");
    var legend = document.getElementById("minerHistoryLegend");

    if (legend) {
      legend.innerHTML = series.map(function (item) {
        var active = nodeHashrateVisible[item.key] !== false;
        return '<button class="miners-history-toggle ' + (active ? "is-active" : "is-muted") + '" type="button" data-series="' + item.key + '">' +
          '<i style="background:' + item.color + '"></i>' + escapeHtml(item.label) +
          '</button>';
      }).join("");
      legend.onclick = function (event) {
        var button = event.target.closest("[data-series]");
        if (!button) return;
        var key = button.getAttribute("data-series");
        nodeHashrateVisible[key] = nodeHashrateVisible[key] === false;
        renderNodeHashrateHistory(pool, history);
      };
    }
    buildHistoryChart(chart, points, visibleSeries, {
      height: 330,
      ariaLabel: "Node hashrate windows"
    });
  }

  var minerTableSort = { key: "h5m", dir: "desc" };
  var lastMinersPayload = null;
  var lastMinersHistory = null;
  var activeMinerAddress = "";
  var activeMinerData = null;
  var statusApiCache = { fetchedAt: 0, payload: null };

  var MINER_TABLE_COLS = [
    { key: "status", label: "Status" },
    { key: "address", label: "Address" },
    { key: "workers", label: "Workers" },
    { key: "h5m", label: "5m" },
    { key: "h1h", label: "1hr" },
    { key: "h1d", label: "1d" },
    { key: "h7d", label: "7d" },
    { key: "bestshare", label: "Best Share" },
    { key: "bestever", label: "Best Ever" },
    { key: "lastshare", label: "Last Share" }
  ];

  function truncAddress(address) {
    var text = toText(address, "--");
    if (text.length <= 26) return text;
    return text.slice(0, 16) + "\u2026" + text.slice(-7);
  }

  function formatClearCountdown(lastshare) {
    var remain = 604800 - (Math.floor(Date.now() / 1000) - (Number(lastshare) || 0));
    if (!Number.isFinite(remain) || remain <= 0) return "";
    var d = Math.floor(remain / 86400);
    var h = Math.floor((remain % 86400) / 3600);
    return '<br><span class="delete-countdown">&#9203; clears in ' + d + "d " + h + "h</span>";
  }

  function minerSortValue(miner, key) {
    if (key === "status") return miner.online ? 1 : 0;
    if (key === "address") return toText(miner.address, "").toLowerCase();
    if (key === "workers") return Number(miner.workers || miner.worker_detail_count) || 0;
    if (key === "h5m") return hashrateToGhs(miner.hashrate5m);
    if (key === "h1h") return hashrateToGhs(miner.hashrate1hr);
    if (key === "h1d") return hashrateToGhs(miner.hashrate1d);
    if (key === "h7d") return hashrateToGhs(miner.hashrate7d);
    if (key === "bestshare") return Number(miner.bestshare) || 0;
    if (key === "bestever") return Number(miner.bestever) || 0;
    if (key === "lastshare") return Number(miner.lastshare) || 0;
    return 0;
  }

  function renderMinersTable(miners) {
    var dir = minerTableSort.dir === "asc" ? 1 : -1;
    var sorted = miners.slice().sort(function (a, b) {
      var av = minerSortValue(a, minerTableSort.key);
      var bv = minerSortValue(b, minerTableSort.key);
      if (typeof av === "string" || typeof bv === "string") {
        return String(av).localeCompare(String(bv)) * dir;
      }
      if (av === bv) return toText(a.address, "").localeCompare(toText(b.address, ""));
      return (av - bv) * dir;
    });
    var head = MINER_TABLE_COLS.map(function (col) {
      var arrow = minerTableSort.key === col.key ? (minerTableSort.dir === "asc" ? " \u2191" : " \u2193") : "";
      return '<th class="sortable" data-mkey="' + col.key + '">' + escapeHtml(col.label + arrow) + '</th>';
    }).join("");
    var rows = sorted.map(function (miner) {
      var online = miner.online === true;
      var clears = online ? "" : formatClearCountdown(miner.lastshare);
      return '<tr class="miner-row' + (online ? "" : " row-offline") + '" data-miner-open="' + escapeHtml(miner.address) + '">' +
        '<td><span class="miner-status-dot ' + (online ? "is-online" : "is-offline") + '"></span></td>' +
        '<td class="mono"><span class="addr-link" title="' + escapeHtml(miner.address) + '">' + escapeHtml(truncAddress(miner.address)) + '</span></td>' +
        '<td>' + escapeHtml(toText(miner.workers || miner.worker_detail_count, "0")) + '</td>' +
        '<td>' + escapeHtml(toText(miner.hashrate5m, "--")) + '</td>' +
        '<td class="cell-accent">' + escapeHtml(toText(miner.hashrate1hr, "--")) + '</td>' +
        '<td class="cell-accent">' + escapeHtml(toText(miner.hashrate1d, "--")) + '</td>' +
        '<td>' + escapeHtml(toText(miner.hashrate7d, "--")) + '</td>' +
        '<td class="cell-yellow">' + escapeHtml(formatShare(miner.bestshare)) + '</td>' +
        '<td class="cell-yellow">' + escapeHtml(formatShare(miner.bestever)) + '</td>' +
        '<td class="cell-muted">' + escapeHtml(toText(miner.lastshare_ago, "-")) + clears + '</td>' +
        '</tr>';
    }).join("");
    return '<div class="table-wrap"><table class="data-table"><thead><tr>' + head + '</tr></thead><tbody>' + rows + '</tbody></table></div>';
  }

  function blockOddsPct(hashrateGhs, periodDays, difficulty) {
    if (!(hashrateGhs > 0) || !(difficulty > 0)) return null;
    var lam = (hashrateGhs * 1e9 * periodDays * 86400) / (difficulty * 4294967296);
    return (1 - Math.exp(-lam)) * 100;
  }

  function renderMinerOdds(miner, statusApi) {
    var diffText = statusApi && statusApi.bitcoin ? statusApi.bitcoin.difficulty_display : "";
    var difficulty = hashrateToGhs(diffText) * 1e9;
    var hr1d = hashrateToGhs(miner && miner.hashrate1d);
    [
      ["minerDetailOdds1d", 1, 4],
      ["minerDetailOdds7d", 7, 4],
      ["minerDetailOdds30d", 30, 3],
      ["minerDetailOdds365d", 365, 3]
    ].forEach(function (cfg) {
      var pct = blockOddsPct(hr1d, cfg[1], difficulty);
      setText(cfg[0], pct === null ? "--" : pct.toFixed(cfg[2]) + "%");
    });
  }

  function fetchMinerDetailStatus(miner) {
    var now = Date.now();
    if (statusApiCache.payload && now - statusApiCache.fetchedAt < WALLET_HISTORY_TTL_MS) {
      renderMinerOdds(miner, statusApiCache.payload);
      return;
    }
    window.fetch("/api/status.cgi?_ts=" + now, { cache: "no-store" })
      .then(function (response) {
        return response.ok ? response.json() : null;
      })
      .then(function (payload) {
        if (payload) statusApiCache = { fetchedAt: Date.now(), payload: payload };
        renderMinerOdds(miner, payload);
      })
      .catch(function () {
        renderMinerOdds(miner, null);
      });
  }

  function formatAuthorised(ts) {
    var d = new Date(ts * 1000);
    if (Number.isNaN(d.getTime())) return "-";
    var p = function (n) {
      return String(n).padStart(2, "0");
    };
    return d.getUTCFullYear() + "-" + p(d.getUTCMonth() + 1) + "-" + p(d.getUTCDate()) +
      " " + p(d.getUTCHours()) + ":" + p(d.getUTCMinutes()) + " UTC";
  }

  function renderMinerDetailWorkers(miner) {
    var box = document.getElementById("minerDetailWorkersTable");
    if (!box) return;
    var workerRows = Array.isArray(miner.workers_detail) ? miner.workers_detail : [];
    if (!workerRows.length) {
      box.innerHTML = '<div class="miners-empty">No active workers.</div>';
      return;
    }
    var sort = getWorkerSortState(miner.address);
    var sorted = sortWorkers(workerRows, sort);
    var cols = [
      ["status", "Status"], ["worker", "Worker Name"], ["h1m", "1m"], ["h5m", "5m"],
      ["h1h", "1hr"], ["h1d", "1d"], ["h7d", "7d"], ["bestshare", "Best Share"],
      ["bestever", "Best Ever"], ["lastseen", "Last Share"]
    ];
    var head = cols.map(function (col) {
      var arrow = sort.key === col[0] ? (sort.dir === "asc" ? " \u2191" : " \u2193") : "";
      return '<th class="sortable" data-wkey="' + col[0] + '">' + escapeHtml(col[1] + arrow) + '</th>';
    }).join("");
    var rows = sorted.map(function (worker) {
      var online = worker.online === true;
      return '<tr class="' + (online ? "" : "row-offline") + '">' +
        '<td><span class="miner-status-dot ' + (online ? "is-online" : "is-offline") + '"></span></td>' +
        '<td class="mono" title="' + escapeHtml(worker.full_name) + '">' + escapeHtml(worker.name || worker.full_name || "worker") + '</td>' +
        '<td>' + escapeHtml(toText(worker.hashrate1m, "--")) + '</td>' +
        '<td>' + escapeHtml(toText(worker.hashrate5m, "--")) + '</td>' +
        '<td class="cell-accent">' + escapeHtml(toText(worker.hashrate1hr, "--")) + '</td>' +
        '<td class="cell-accent">' + escapeHtml(toText(worker.hashrate1d, "--")) + '</td>' +
        '<td>' + escapeHtml(toText(worker.hashrate7d, "--")) + '</td>' +
        '<td class="cell-yellow">' + escapeHtml(formatShare(worker.bestshare)) + '</td>' +
        '<td class="cell-yellow">' + escapeHtml(formatShare(worker.bestever)) + '</td>' +
        '<td class="cell-muted">' + escapeHtml(toText(worker.lastshare_ago, "-")) + '</td>' +
        '</tr>';
    }).join("");
    box.innerHTML = '<div class="table-wrap"><table class="data-table"><thead><tr>' + head + '</tr></thead><tbody>' + rows + '</tbody></table></div>';
    box.onclick = function (event) {
      var th = event.target.closest("[data-wkey]");
      if (!th) return;
      var key = th.getAttribute("data-wkey");
      sort.dir = sort.key === key && sort.dir === "desc" ? "asc" : "desc";
      sort.key = key;
      renderMinerDetailWorkers(miner);
    };
  }

  function openMinerDetail(address) {
    activeMinerAddress = address;
    setActiveView("miner");
    renderMinerDetail();
  }

  function renderMinerDetail() {
    var address = activeMinerAddress;
    if (!address) return;
    var payload = lastMinersPayload || {};
    var miners = Array.isArray(payload.miners) ? payload.miners : [];
    var miner = null;
    miners.forEach(function (item) {
      if (item.address === address) miner = item;
    });
    if (!miner) {
      setText("minerDetailAddress", address);
      setBadgeState("minerDetailBadge", "No Data", "status-soft");
      return;
    }
    activeMinerData = miner;
    setText("minerDetailAddress", miner.address);
    setBadgeState("minerDetailBadge", miner.online ? "\u25CF Online" : "\u25CF Offline", miner.online ? "status-ok" : "status-soft");
    var authorised = Number(miner.authorised) || 0;
    setText("minerDetailAuthorised", authorised > 0 ? "Authorised " + formatAuthorised(authorised) : "Not authorised");
    setText("minerDetailLastShare", "Last share " + toText(miner.lastshare_ago, "-"));
    var wcount = Number(miner.workers || miner.worker_detail_count) || 0;
    setText("minerDetailWorkersChip", wcount + " worker" + (wcount === 1 ? "" : "s"));
    setText("minerDetailHash5m", toText(miner.hashrate5m, "--"));
    setText("minerDetailShares", formatShare(miner.shares));
    setText("minerDetailBestShare", formatShare(miner.bestshare));
    setText("minerDetailBestEver", formatShare(miner.bestever));
    setText("minerDetailWorkers", formatCount(wcount));
    renderMinerDetailWorkers(miner);
    fetchWalletHistory(address);
    fetchMinerDetailStatus(miner);
  }

  function renderMiners(data, history) {
    var payload = data || {};
    var pool = payload.pool || {};
    var miners = Array.isArray(payload.miners) ? payload.miners : [];
    var users = Number(pool.users);
    var workers = Number(pool.workers);

    setText("minerUsers", Number.isFinite(users) ? formatCount(users) : formatCount(miners.length));
    setText("minerWorkers", Number.isFinite(workers) ? formatCount(workers) : "--");
    setText("minerHash5m", toText(pool.hashrate5m, "--"));
    setText("minerBestShare", formatShare(pool.bestshare));
    /* redesign: Miners view summary mirrors of the dashboard KPI cards */
    setText("summaryWorkers", Number.isFinite(workers) ? formatCount(workers) : "--");
    setText("summaryHash5m", toText(pool.hashrate5m, "--"));
    setText("summaryBestShare", formatShare(pool.bestshare));
    setText("minersUpdated", formatApiAge(payload.generated_at) + " · next cache refresh " + toText(payload.next_refresh_seconds, "60") + "s");
    setText("minersSource", toText(payload.source && payload.source.users_dir, "/root/ckpool/logs/users"));

    /* redesign: hero miners badge — render-only, reuses the cached miners.cgi payload above */
    var onlineMiners = miners.filter(function (miner) { return miner.online; }).length;
    setBadgeState("heroMinersBadge",
      miners.length ? onlineMiners + " / " + miners.length + " online" : "No miners",
      onlineMiners > 0 ? "status-ok" : "status-soft");
    renderNodeHashrateHistory(pool, history);

    lastMinersPayload = payload;
    lastMinersHistory = history;
    if (activeViewName === "miner" && activeMinerAddress) renderMinerDetail();

    var list = document.getElementById("minersList");
    if (!list) return;

    if (!miners.length) {
      list.innerHTML = '<div class="miners-empty">' + escapeHtml(payload.error || "No miners found yet. Connect a miner to the local Stratum endpoint.") + '</div>';
      return;
    }

    list.innerHTML = renderMinersTable(miners);

    list.onclick = function (event) {
      var th = event.target.closest("[data-mkey]");
      if (th) {
        var key = th.getAttribute("data-mkey");
        minerTableSort.dir = minerTableSort.key === key && minerTableSort.dir === "desc" ? "asc" : "desc";
        minerTableSort.key = key;
        renderMiners(payload, history);
        return;
      }
      var row = event.target.closest("[data-miner-open]");
      if (row) {
        event.preventDefault();
        openMinerDetail(row.getAttribute("data-miner-open"));
      }
    };
  }

  function fetchMinersData() {
    if (minersLoading) return Promise.resolve(null);
    minersLoading = true;
    return Promise.all([
      window.fetch("/api/miners.cgi?_ts=" + Date.now(), { cache: "no-store" }).then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      }),
      window.fetch("/api/miners-history.cgi?_ts=" + Date.now(), { cache: "no-store" }).then(function (response) {
        if (!response.ok) return null;
        return response.json();
      }).catch(function () {
        return null;
      })
    ])
      .then(function (results) {
        var payload = results[0];
        var history = results[1];
        renderMiners(payload, history);
        minersLoaded = true;
        return payload;
      })
      .catch(function () {
        if (!minersLoaded) {
          renderMiners({ ok: false, error: "Miners API unavailable.", pool: {}, miners: [] }, null);
        }
        return null;
      })
      .then(function (result) {
        minersLoading = false;
        return result;
      });
  }

  function startMinersPolling() {
    stopMinersPolling();
    fetchMinersData();
    minersPollTimer = window.setInterval(function () {
      if (activeViewName === "miners" || activeViewName === "miner") fetchMinersData();
    }, MINERS_REFRESH_SECONDS * 1000);
  }

  function stopWifiPolling() {
    if (wifiPollTimer) {
      window.clearInterval(wifiPollTimer);
      wifiPollTimer = null;
    }
  }

  function setWifiMessage(text) {
    setText("wifiMessage", text || "Wi-Fi changes only reconfigure the network stack. On a single-radio board, scan may pause Wi-Fi briefly. Bitcoind data stays on /root/block.");
  }

  function setWifiControlsDisabled(disabled) {
    var buttons = ["wifiScanBtn", "wifiApplyBtn", "wifiSsidSelect", "wifiSsidManual", "wifiPassword"];
    buttons.forEach(function (id) {
      var el = document.getElementById(id);
      if (el) el.disabled = !!disabled;
    });
  }

  function setWifiScanAvailable(available) {
    var el = document.getElementById("wifiScanBtn");
    if (el) el.disabled = !available || !!wifiState.applying || !!wifiState.scanning;
  }

  function wifiSelectManualVisible(show) {
    var field = document.getElementById("wifiManualField");
    if (field) field.hidden = !show;
  }

  function wifiSelectedValue() {
    var select = document.getElementById("wifiSsidSelect");
    var manual = document.getElementById("wifiSsidManual");
    if (!select) return "";
    if (select.value === "__custom__") return manual ? manual.value.trim() : "";
    return select.value;
  }

  function buildWifiOptions(wifi) {
    var select = document.getElementById("wifiSsidSelect");
    if (!select) return;

    var current = wifi || mock.wifi;
    var preferred = current.savedSsid && current.savedSsid !== "-" ? current.savedSsid : current.connectedSsid;
    var seen = {};
    var options = [];

    function addOption(ssid, label) {
      if (!ssid || seen[ssid]) return;
      seen[ssid] = true;
      options.push({ value: ssid, label: label || ssid });
    }

    addOption(preferred, preferred);
    (current.scanResults || []).forEach(function (item) {
      if (!item || !item.ssid) return;
      var label = item.ssid;
      if (item.signal !== undefined && item.signal !== null && item.signal !== "") {
        label += " (" + item.signal + " dBm)";
      }
      addOption(item.ssid, label);
    });

    select.innerHTML = "";
    var placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = options.length ? "Choose a Wi-Fi network" : "Scan to discover networks";
    select.appendChild(placeholder);

    options.forEach(function (item) {
      var opt = document.createElement("option");
      opt.value = item.value;
      opt.textContent = item.label;
      select.appendChild(opt);
    });

    var custom = document.createElement("option");
    custom.value = "__custom__";
    custom.textContent = "Custom / Hidden SSID";
    select.appendChild(custom);

    var target = preferred || (options[0] && options[0].value) || "__custom__";
    if (target && seen[target]) {
      select.value = target;
      wifiSelectManualVisible(false);
    } else {
      select.value = "__custom__";
      wifiSelectManualVisible(true);
    }
  }

  function renderWifiView(wifi) {
    var data = wifi || mock.wifi;
    wifiState.current = data;
    setBadgeState("wifiStatusBadge", data.statusBadge, data.statusClass);
    setText("wifiSavedSsid", data.savedSsid);
    setText("wifiConnectedSsid", data.connectedSsid);
    setText("wifiConnectedIp", data.connectedIp);
    setText("wifiApplyState", data.applyState);
    setWifiMessage(data.applyMessage || "Wi-Fi changes only reconfigure the network stack. On a single-radio board, scan may pause Wi-Fi briefly. Bitcoind data stays on /root/block.");
    buildWifiOptions(data);

    var manual = document.getElementById("wifiSsidManual");
    var password = document.getElementById("wifiPassword");
    var select = document.getElementById("wifiSsidSelect");
    if (manual && data.savedSsid && data.savedSsid !== "-") manual.value = data.savedSsid;
    if (password && !password.value) password.value = "";
    if (select && select.value === "__custom__" && manual) {
      manual.value = data.savedSsid && data.savedSsid !== "-" ? data.savedSsid : "";
    }
    setWifiControlsDisabled(data.applyState === "applying");
    setWifiScanAvailable(!!data.scanSupported);
  }

  function wifiRequest(action, options) {
    var opt = options || {};
    var seq = ++wifiRequestSeq;
    var fetchOptions = {
      cache: "no-store"
    };
    var url = "/api/wifi.cgi?action=" + encodeURIComponent(action) + "&_ts=" + Date.now();

    if (opt.method === "POST") {
      fetchOptions.method = "POST";
      fetchOptions.headers = { "Content-Type": "application/x-www-form-urlencoded" };
      fetchOptions.body = opt.body || "";
    }

    return window.fetch(url, fetchOptions)
      .then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      })
      .then(function (payload) {
        if (seq !== wifiRequestSeq) return null;
        return payload;
      });
  }

  function loadWifiSettings() {
    if (wifiState.loading) return Promise.resolve(null);
    wifiState.loading = true;
    return wifiRequest("get")
      .then(function (payload) {
        if (!payload) return null;
        wifiState.loaded = true;
        wifiState.current = normalizeWifiData(payload);
        renderWifiView(wifiState.current);
        if (wifiState.current.applyState === "applying") {
          startWifiApplyPolling();
        } else {
          stopWifiPolling();
        }
        return wifiState.current;
      })
      .catch(function () {
        renderWifiView(mock.wifi);
        setWifiMessage("Wi-Fi settings are unavailable right now.");
        return null;
      })
      .then(function (result) {
        wifiState.loading = false;
        return result;
      });
  }

  function refreshWifiScan() {
    if (wifiState.scanning) return Promise.resolve(null);
    if (!wifiState.current || !wifiState.current.scanSupported) {
      setWifiMessage("Wi-Fi scanning is not available on this image.");
      return Promise.resolve(null);
    }
    wifiState.scanning = true;
    setWifiMessage("Scanning nearby Wi-Fi networks...");
    setWifiControlsDisabled(true);
    return wifiRequest("scan")
      .then(function (payload) {
        if (!payload) return null;
        wifiState.current.scanResults = Array.isArray(payload.scan) ? payload.scan : [];
        wifiState.current.scanSupported = true;
        renderWifiView(wifiState.current);
        setWifiMessage(wifiState.current.scanResults.length ? "Select a network and apply the profile." : "No Wi-Fi networks found.");
        return wifiState.current.scanResults;
      })
      .catch(function () {
        setWifiMessage("Wi-Fi scan failed. Make sure wlan0 is up and wpa_supplicant is running.");
        renderWifiView(wifiState.current);
        return null;
      })
      .then(function (result) {
        wifiState.scanning = false;
        setWifiScanAvailable(!!(wifiState.current && wifiState.current.scanSupported));
        return result;
      });
  }

  function startWifiApplyPolling() {
    stopWifiPolling();
    wifiPollTimer = window.setInterval(function () {
      if (!wifiState.applying) {
        loadWifiSettings();
        return;
      }
      loadWifiSettings().then(function (data) {
        if (!data) return;
        if (data.applyState !== "applying") {
          wifiState.applying = false;
          stopWifiPolling();
          setWifiControlsDisabled(false);
          setWifiMessage(data.applyMessage || "Wi-Fi profile updated.");
        }
      });
    }, 2000);
  }

  function applyWifiProfile() {
    var select = document.getElementById("wifiSsidSelect");
    var manual = document.getElementById("wifiSsidManual");
    var password = document.getElementById("wifiPassword");
    var ssid = wifiSelectedValue();
    var hidden = select && select.value === "__custom__" ? "1" : "0";
    var pass = password ? password.value : "";

    if (!ssid) {
      setWifiMessage("SSID is required.");
      return Promise.resolve(null);
    }

    wifiState.applying = true;
    setWifiControlsDisabled(true);
    setWifiMessage("Applying Wi-Fi profile. The device may reconnect for a few seconds.");

    var body = "ssid=" + encodeURIComponent(ssid) +
      "&password=" + encodeURIComponent(pass) +
      "&hidden=" + encodeURIComponent(hidden);

    return wifiRequest("apply", {
      method: "POST",
      body: body
    }).then(function (payload) {
      if (!payload || payload.ok === false) {
        wifiState.applying = false;
        setWifiControlsDisabled(false);
        setWifiMessage((payload && payload.error) ? payload.error : "Failed to start Wi-Fi apply.");
        return null;
      }
      startWifiApplyPolling();
      return payload;
    }).catch(function () {
      wifiState.applying = false;
      setWifiControlsDisabled(false);
      setWifiMessage("Failed to start Wi-Fi apply.");
      return null;
    });
  }

  function getRssiLevel(rssi) {
    if (!rssi && rssi !== 0) return 0;
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    return 1;
  }

  function getRssiColor(rssi) {
    if (!rssi && rssi !== 0) return "#555";
    if (rssi >= -50) return "#3fb950";
    if (rssi >= -60) return "#3fb950";
    if (rssi >= -70) return "#d29922";
    return "#f85149";
  }

  function getStoredTheme() {
    var theme = "";
    try {
      theme = window.localStorage.getItem("nmTheme") || "";
    } catch (e) {}
    return theme === "light" ? "light" : "dark";
  }

  function setTheme(theme) {
    var nextTheme = theme === "dark" ? "dark" : "light";
    var root = document.documentElement;
    var label = document.getElementById("themeToggleLabel");
    var icon = document.getElementById("themeToggleIcon");
    root.setAttribute("data-theme", nextTheme);
    if (label) label.textContent = themes[nextTheme].label;
    if (icon) icon.textContent = themes[nextTheme].icon;
    try {
      window.localStorage.setItem("nmTheme", nextTheme);
    } catch (e) {}
  }

  function renderDashboard(viewData) {
    var data = viewData || mock;
    var blockParts = String(data.bitcoin.blocks || "-- / --").split(" / ");
    var stageHeight = toText(data.bitcoin.ibdStageHeight, "");
    var isIbdStage = data.bitcoin.ibdStageKey === "pre_headers" || data.bitcoin.ibdStageKey === "sync_headers";

    setText("cpuPct", formatPercent(data.system.cpuPct, 0));
    setText("cpuLoadValue", data.system.cpuLoad);
    setText("cpuLoadAvgValue", data.system.loadAverage);
    setText("diskPct", formatPercent(data.system.diskPct, 0));
    setText("diskValue", data.system.diskUsage);
    setText("memPct", formatPercent(data.system.memPct, 0));
    setText("memValue", data.system.memUsage);
    setText("swapPct", formatPercent(data.system.swapPct, 0));
    setText("swapValue", data.system.swapUsage);
    setProgress("cpuOrb", data.system.cpuPct);
    setProgress("diskOrb", data.system.diskPct);
    setProgress("memOrb", data.system.memPct);
    setProgress("swapOrb", data.system.swapPct);

    setBadgeState("pruneBadge", data.bitcoin.pruneBadge, data.bitcoin.pruneClass);
    setIbdStage("ibdStage", data.bitcoin.ibdStageKey, data.bitcoin.syncDetail, data.bitcoin.bestBlockDate);
    setRollingValue("ibdPct", formatIbdPercent(data.bitcoin.progressPct));
    if (isIbdStage && stageHeight) {
      setRollingValue("ibdBlocksCurrent", stageHeight);
      setText("ibdBlocksTotal", "");
      var divider = document.getElementById("ibdBlocksDivider");
      if (divider) divider.style.display = "none";
    } else {
      setRollingValue("ibdBlocksCurrent", blockParts[0] || "--");
      setRollingValue("ibdBlocksTotal", blockParts[1] || "--");
      var dividerShow = document.getElementById("ibdBlocksDivider");
      if (dividerShow) dividerShow.style.display = "";
    }
    setIbdMeta("ibdMeta", data.bitcoin.chainName, data.bitcoin.bestBlockDate, data.bitcoin.ibdStageKey);
    setRollingValue("nodeConn", data.bitcoin.connections);
    setRollingValue("nodeDiff", data.bitcoin.difficulty);
    setRollingValue("nodeRss", data.bitcoin.rss);
    setRollingValue("nodeVersion", data.bitcoin.version);
    setRollingValue("nodeUptime", data.bitcoin.uptime);

    var orb = document.getElementById("ibdOrb");
    if (orb) orb.style.setProperty("--pct", data.bitcoin.progressPct);

    setText("stratumUrl", data.stratum.url);
    setText("stratumUser", data.stratum.user);
    setText("stratumPass", data.stratum.pass);
    setText("stratumMode", data.stratum.mode);

    /* redesign: hero status strip — render-only, reuses the normalized status.cgi data above */
    setBadgeState("heroBitcoindBadge", data.bitcoin.syncBadge, data.bitcoin.syncClass);
    setBadgeState("heroStratumBadge", data.stratum.badge, data.stratum.badgeClass);

    setText("sysCpu", data.system.cpuModel);
    setText("sysTemp", data.system.temperature);
    var tempNum = parseFloat(data.system.temperature);
    var tempValid = Number.isFinite(tempNum);
    setText("tempPct", tempValid ? Math.round(tempNum) + "°" : "--");
    setProgress("tempOrb", tempValid ? Math.max(0, Math.min(100, tempNum)) : 0);
    setText("sysBlock", data.system.rootfsUsage);

    setText("netIp", data.network.ip);
    setText("netSsid", data.network.ssid);
    setText("netMac", data.network.mac);
    setText("netRssi", data.network.rssi);

    var rssiBars = document.querySelectorAll("[data-rssi-bar]");
    var rssiLevel = toNumber(data.network.signalLevel, getRssiLevel(data.network.rssi));
    var rssiColor = getRssiColor(parseRssiDbm(data.network.rssi));
    rssiBars.forEach(function (bar, index) {
      bar.style.backgroundColor = index < rssiLevel ? rssiColor : "#555";
    });

    setText("aboutFirmware", data.about.firmware);
    setText("aboutUpdated", data.about.updated);
  }

  function fetchDashboardData() {
    if (refreshInFlight) return Promise.resolve(null);
    refreshInFlight = true;
    var controller = typeof AbortController === "function" ? new AbortController() : null;
    var timeoutId = controller ? window.setTimeout(function () {
      controller.abort();
    }, 4500) : 0;
    return window.fetch("/api/status.cgi?_ts=" + Date.now(), {
      signal: controller ? controller.signal : undefined,
      cache: "no-store"
    }).then(function (response) {
      if (!response.ok) throw new Error("HTTP " + response.status);
      return response.json();
    }).then(function (api) {
      var viewData = normalizeDashboardData(api);
      renderDashboard(viewData);
      hasLoadedDashboardData = true;
      completeRefreshCycle();
      return viewData;
    }).catch(function () {
      if (!hasLoadedDashboardData) renderDashboard(mock);
      completeRefreshCycle();
      return null;
    }).finally(function () {
      if (timeoutId) window.clearTimeout(timeoutId);
    });
  }

  function setActiveView(view) {
    var buttons = document.querySelectorAll(".nav-item");
    var panels = document.querySelectorAll(".panel-view");
    var navView = view === "miner" ? "miners" : view;

    buttons.forEach(function (button) {
      button.classList.toggle("is-active", button.getAttribute("data-view") === navView);
    });

    panels.forEach(function (panel) {
      panel.classList.toggle("is-visible", panel.getAttribute("data-view-panel") === view);
    });

    activeViewName = view;
    if (view === "logs") startLogPolling(activeLogService || "bitcoind");
    else stopLogPolling();
    if (view === "miners" || view === "miner") startMinersPolling();
    else stopMinersPolling();
    if (view === "settings") loadWifiSettings();
    else if (!wifiState.applying) stopWifiPolling();

    window.location.hash = view === "miner" && activeMinerAddress ? "miner/" + activeMinerAddress : view;
  }

  function bindNav() {
    var buttons = document.querySelectorAll(".nav-item");
    buttons.forEach(function (button) {
      button.addEventListener("click", function () {
        setActiveView(button.getAttribute("data-view"));
      });
    });
    var back = document.getElementById("minerDetailBack");
    if (back) {
      back.addEventListener("click", function () {
        setActiveView("miners");
      });
    }
  }

  function bindThemeToggle() {
    var toggle = document.getElementById("themeToggle");
    if (!toggle) return;
    toggle.addEventListener("click", function () {
      var current = document.documentElement.getAttribute("data-theme") || "light";
      setTheme(current === "dark" ? "light" : "dark");
    });
  }

  function bindWifiControls() {
    var select = document.getElementById("wifiSsidSelect");
    var manual = document.getElementById("wifiSsidManual");
    var scanBtn = document.getElementById("wifiScanBtn");
    var applyBtn = document.getElementById("wifiApplyBtn");

    if (select) {
      select.addEventListener("change", function () {
        wifiSelectManualVisible(select.value === "__custom__");
        if (select.value !== "__custom__" && manual) {
          manual.value = select.value;
        }
      });
    }

    if (scanBtn) {
      scanBtn.addEventListener("click", function () {
        refreshWifiScan();
      });
    }

    if (applyBtn) {
      applyBtn.addEventListener("click", function () {
        applyWifiProfile();
      });
    }
  }

  function init() {
    renderDashboard(mock);
    setRefreshCountdown(DASHBOARD_REFRESH_SECONDS);
    bindNav();
    bindLogTabs();
    bindWifiControls();
    bindThemeToggle();
    setTheme(getStoredTheme());
    fetchDashboardData();
    /* prefetch miners cache so the Stratum card pool stats and hero Miners badge fill on first load */
    fetchMinersData();
    startRefreshCountdown();

    var initial = window.location.hash ? window.location.hash.slice(1) : "dashboard";
    if (initial.indexOf("miner/") === 0) {
      activeMinerAddress = initial.slice(6);
      initial = "miner";
    }
    initial = viewAliases[initial] || initial;
    if (!validViews[initial]) initial = "dashboard";
    setActiveView(initial);
    if (initial === "miner") renderMinerDetail();
  }

  init();
})();

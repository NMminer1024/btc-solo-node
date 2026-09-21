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
      uptime: "--",
      productVersion: "--"
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
      utxoSize: "--",
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
      applyMessage: ""
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
  var lastDashboardApi = null;
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
    applying: false,
    current: mock.wifi
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
      path: "/root/config/ckpool/logs/ckpool.log",
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
    setText("logsPanel", state.text || t("logs.empty"));
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
    setLogSummary(service, getLogState(service).loaded ? t("logs.polling") : t("logs.summary"));
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

  // Reformat a UTC timestamp ("YYYY-MM-DD HH:MM:SS UTC") into the browser's
  // local timezone so users see a familiar local time. Falls back to the
  // original string when it does not match the expected shape.
  function formatUtcToLocal(value) {
    var text = String(value || "");
    var match = text.match(/^(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})/);
    if (!match) return text;
    var ts = Date.UTC(+match[1], +match[2] - 1, +match[3], +match[4], +match[5], +match[6]);
    var d = new Date(ts);
    if (isNaN(d.getTime())) return text;
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) + " " +
           pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds());
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
    var syncKey = toText(bitcoin.sync_key, "");
    var pruneKey = toText(bitcoin.prune_key, "");
    var stratumKey = toText(mining.ckpool_state, "");
    var syncLabel = t("label.sync." + syncKey) !== "label.sync." + syncKey ? t("label.sync." + syncKey) : (labels.sync[syncKey] || mock.bitcoin.syncBadge);
    var pruneLabel = t("label.prune." + pruneKey) !== "label.prune." + pruneKey ? t("label.prune." + pruneKey) : (labels.prune[pruneKey] || mock.bitcoin.pruneBadge);
    var stratumLabel = t("label.stratum." + stratumKey) !== "label.stratum." + stratumKey ? t("label.stratum." + stratumKey) : (labels.stratum[stratumKey] || mock.stratum.badge);
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
        uptime: toText(system.uptime, mock.system.uptime),
        productVersion: toText(system.product_version, mock.system.productVersion)
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
        bestBlockDate: formatUtcToLocal(toText(bitcoin.best_block_date, "-")),
        meta: joinParts([chainName, formatUtcToLocal(toText(bitcoin.best_block_date, "-"))], mock.bitcoin.meta),
        connections: toText(bitcoin.connections, mock.bitcoin.connections),
        difficulty: toText(bitcoin.difficulty_display, mock.bitcoin.difficulty),
        utxoSize: toText(bitcoin.utxo_size, mock.bitcoin.utxoSize),
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
      about: {
        firmware: toText(system.product_version, mock.about.firmware),
        updated: toText(system.build_date, mock.about.updated)
      }
    };
  }

  function normalizeWifiData(api) {
    if (!api || typeof api !== "object") return mock.wifi;

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
    var labelCount = 4;
    var labelParts = [];
    for (var li = 0; li < labelCount; li += 1) {
      var lIndex = Math.round(li * span / (labelCount - 1));
      var lx = left + (width - left - right) * lIndex / span;
      labelParts.push('<text x="' + lx.toFixed(1) + '" y="' + (height - 9) + '" text-anchor="middle">' + escapeHtml(formatChartTime(points[lIndex].ts)) + '</text>');
    }
    var labels = labelParts.join("");
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
      ariaLabel: "Hashrate history",
      emptyHtml: '<div class="miners-empty">No data yet</div>'
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
      ariaLabel: "Worker history",
      emptyHtml: '<div class="miners-empty">No data yet</div>'
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
      box.innerHTML = '<div class="miners-empty">' + escapeHtml(t("miners.noWorkers")) + '</div>';
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
      setBadgeState("minerDetailBadge", t("miner.noData"), "status-soft");
      return;
    }
    activeMinerData = miner;
    setText("minerDetailAddress", miner.address);
    setBadgeState("minerDetailBadge", miner.online ? t("miner.online") : t("miner.offline"), miner.online ? "status-ok" : "status-soft");
    var authorised = Number(miner.authorised) || 0;
    setText("minerDetailAuthorised", authorised > 0 ? t("miner.authorised") + " " + formatAuthorised(authorised) : t("miner.notAuthorised"));
    setText("minerDetailLastShare", t("miner.lastShare") + " " + toText(miner.lastshare_ago, "-"));
    var wcount = Number(miner.workers || miner.worker_detail_count) || 0;
    setText("minerDetailWorkersChip", wcount + " " + t(wcount === 1 ? "miner.worker" : "miner.workers"));
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
    setText("minerWorkers", miners.length ? formatCount(workers) + " / " + formatCount(miners.length) : "--");
    setText("minerHash5m", toText(pool.hashrate5m, "--"));
    setText("minerBestShare", formatShare(pool.bestshare));
    /* redesign: Miners view summary mirrors of the dashboard KPI cards */
    setText("summaryWorkers", Number.isFinite(workers) ? formatCount(workers) : "--");
    setText("summaryHash5m", toText(pool.hashrate5m, "--"));
    setText("summaryBestShare", formatShare(pool.bestshare));
    setText("minersUpdated", formatApiAge(payload.generated_at) + " · next cache refresh " + toText(payload.next_refresh_seconds, "60") + "s");
    setText("minersSource", toText(payload.source && payload.source.users_dir, "/root/config/ckpool/logs/users"));

    renderNodeHashrateHistory(pool, history);

    lastMinersPayload = payload;
    lastMinersHistory = history;
    if (activeViewName === "miner" && activeMinerAddress) renderMinerDetail();

    var list = document.getElementById("minersList");
    if (!list) return;

    if (!miners.length) {
      list.innerHTML = '<div class="miners-empty">' + escapeHtml(payload.error || t("miners.noMiners")) + '</div>';
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

  /* ── Wi-Fi credential history (localStorage) ─────────────
   * Remember previously entered SSID + password pairs so the user can pick
   * one back from the SSID datalist and have the password auto-filled.
   * Stored as a JSON array of {ssid, pass} objects, newest first, capped at
   * 8 entries. This is a local-only convenience; the board itself keeps the
   * active profile in /root/config/rockbase-wifi.conf. */
  var WIFI_HISTORY_KEY = "nmWifiHistory";
  var WIFI_HISTORY_MAX = 8;

  function loadWifiHistory() {
    try {
      var raw = window.localStorage.getItem(WIFI_HISTORY_KEY);
      var arr = raw ? JSON.parse(raw) : [];
      return Array.isArray(arr) ? arr : [];
    } catch (e) {
      return [];
    }
  }

  function saveWifiHistory(entry) {
    if (!entry || !entry.ssid) return;
    var list = loadWifiHistory().filter(function (item) {
      return item && item.ssid !== entry.ssid;
    });
    list.unshift({ ssid: entry.ssid, pass: entry.pass || "" });
    if (list.length > WIFI_HISTORY_MAX) list.length = WIFI_HISTORY_MAX;
    try {
      window.localStorage.setItem(WIFI_HISTORY_KEY, JSON.stringify(list));
    } catch (e) {}
  }

  function renderWifiSsidHistory() {
    var dl = document.getElementById("wifiSsidHistory");
    if (!dl) return;
    dl.innerHTML = "";
    loadWifiHistory().forEach(function (item) {
      if (!item || !item.ssid) return;
      var opt = document.createElement("option");
      opt.value = item.ssid;
      dl.appendChild(opt);
    });
  }

  function setWifiMessage(text) {
    setText("wifiMessage", text || t("settings.wifiNote"));
  }

  function setWifiControlsDisabled(disabled) {
    var ids = ["wifiApplyBtn", "wifiSsid", "wifiPassword"];
    ids.forEach(function (id) {
      var el = document.getElementById(id);
      if (el) el.disabled = !!disabled;
    });
  }

  function renderWifiView(wifi) {
    var data = wifi || mock.wifi;
    wifiState.current = data;
    setBadgeState("wifiStatusBadge", data.statusBadge, data.statusClass);
    setWifiMessage(data.applyMessage || "Enter your Wi-Fi SSID and password, then tap Apply. The device will reconnect automatically.");

    var ssidInput = document.getElementById("wifiSsid");
    if (ssidInput && data.savedSsid && data.savedSsid !== "-" && !ssidInput.value) {
      ssidInput.value = data.savedSsid;
    }
    renderWifiSsidHistory();
    setWifiControlsDisabled(data.applyState === "applying");
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
    var ssidInput = document.getElementById("wifiSsid");
    var password = document.getElementById("wifiPassword");
    var ssid = ssidInput ? ssidInput.value.trim() : "";
    var pass = password ? password.value : "";

    if (!ssid) {
      setWifiMessage("SSID is required.");
      return Promise.resolve(null);
    }
    if (ssid.length > 32) {
      setWifiMessage("SSID must be 32 characters or fewer.");
      return Promise.resolve(null);
    }
    if (pass && (pass.length < 8 || pass.length > 63)) {
      setWifiMessage("Password must be 8-63 characters (or leave blank for open network).");
      return Promise.resolve(null);
    }

    wifiState.applying = true;
    setWifiControlsDisabled(true);
    setWifiMessage("Applying Wi-Fi profile. The device may reconnect for a few seconds.");

    var body = "ssid=" + encodeURIComponent(ssid) +
      "&password=" + encodeURIComponent(pass) +
      "&hidden=1";

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
      // Remember this credential pair for next time (best-effort).
      saveWifiHistory({ ssid: ssid, pass: pass });
      renderWifiSsidHistory();
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
    if (label) label.textContent = t("theme." + nextTheme);
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
    setRollingValue("nodeUtxoSize", data.bitcoin.utxoSize);
    setRollingValue("nodeUptime", data.bitcoin.uptime);

    var orb = document.getElementById("ibdOrb");
    if (orb) {
      orb.style.setProperty("--pct", data.bitcoin.progressPct);
      // Show the check mark instead of the percentage once fully synced.
      var complete = Number(data.bitcoin.progressPct) >= 100;
      orb.classList.toggle("is-complete", complete);
    }

    setText("stratumUrl", data.stratum.url);

    /* card title badges — version + status inline in card headers */
    setBadgeState("cardBitcoindBadge", data.bitcoin.syncBadge, data.bitcoin.syncClass);
    setBadgeState("cardStratumBadge", data.stratum.badge, data.stratum.badgeClass);
    var cardVer = String(data.bitcoin.version || "").replace(/^Satoshi:/i, "").replace(/\//g, "").trim();
    setText("cardBitcoindVersion", cardVer && cardVer !== "--" ? cardVer : "");

    setText("sysCpu", "Canaan K230 \u00b7 RISC-V");
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

    setText("aboutFirmware", data.system.productVersion);
    setText("aboutUpdated", data.about.updated);
    setText("dashVersion", data.system.productVersion);
    currentProductVersion = data.system.productVersion;
    setText("aboutCurrentVer", data.system.productVersion);
    setText("aboutProductVer", data.system.productVersion);
    setText("aboutUptime", data.system.uptime);
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
      lastDashboardApi = api;
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
    if (view === "settings") {
      loadWifiSettings();
      loadTimezone();
    } else if (!wifiState.applying) stopWifiPolling();

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

  function copyTextToClipboard(text, done) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done, function () {
        copyTextFallback(text);
        done();
      });
      return;
    }
    copyTextFallback(text);
    done();
  }

  function copyTextFallback(text) {
    var area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.style.position = "fixed";
    area.style.opacity = "0";
    document.body.appendChild(area);
    area.select();
    try {
      document.execCommand("copy");
    } catch (e) {}
    document.body.removeChild(area);
  }

  function bindStratumCopy() {
    var btn = document.getElementById("stratumCopyBtn");
    if (!btn) return;
    btn.addEventListener("click", function () {
      var endpoint = document.getElementById("stratumUrl");
      var text = endpoint ? endpoint.textContent.trim() : "";
      if (!text) return;
      copyTextToClipboard(text, function () {
        btn.classList.add("is-copied");
        window.setTimeout(function () {
          btn.classList.remove("is-copied");
        }, 1200);
      });
    });
  }

  /* ── Timezone ────────────────────────────────────────────
   * The zone list mirrors package/rockbase_lcdui/src/timezone_overlay.h
   * kZones[] (the single source of truth for the LCD UI). Keep them in sync. */
  var TZ_ZONES = [
    ["Africa/Cairo", "+2"],
    ["America/Anchorage", "-9"],
    ["America/Chicago", "-6"],
    ["America/Denver", "-7"],
    ["America/Los_Angeles", "-8"],
    ["America/New_York", "-5"],
    ["America/Santiago", "-4"],
    ["America/Sao_Paulo", "-3"],
    ["Asia/Bangkok", "+7"],
    ["Asia/Dhaka", "+6"],
    ["Asia/Dubai", "+4"],
    ["Asia/Kabul", "+4:30"],
    ["Asia/Karachi", "+5"],
    ["Asia/Kolkata", "+5:30"],
    ["Asia/Shanghai", "+8"],
    ["Asia/Tehran", "+3:30"],
    ["Asia/Tokyo", "+9"],
    ["Asia/Yangon", "+6:30"],
    ["Atlantic/Azores", "-1"],
    ["Australia/Adelaide", "+9:30"],
    ["Australia/Perth", "+8"],
    ["Australia/Sydney", "+10"],
    ["Europe/Berlin", "+1"],
    ["Europe/Moscow", "+3"],
    ["Pacific/Auckland", "+12"],
    ["Pacific/Honolulu", "-10"],
    ["Pacific/Noumea", "+11"],
    ["Pacific/Pago_Pago", "-11"],
    ["UTC", "+0"]
  ];

  var tzState = { loading: false, applying: false, current: "", open: false };

  function tzRequest(action, options) {
    var opt = options || {};
    var fetchOptions = { cache: "no-store" };
    var url = "/api/timezone.cgi?action=" + encodeURIComponent(action) + "&_ts=" + Date.now();
    if (opt.method === "POST") {
      fetchOptions.method = "POST";
      fetchOptions.headers = { "Content-Type": "application/x-www-form-urlencoded" };
      fetchOptions.body = opt.body || "";
    }
    return window.fetch(url, fetchOptions)
      .then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      });
  }

  function tzOffsetLabel(offset) {
    return "UTC" + offset;
  }

  function buildTzList(selected) {
    var list = document.getElementById("tzSelectList");
    if (!list) return;
    list.innerHTML = "";
    TZ_ZONES.forEach(function (zone) {
      var li = document.createElement("li");
      li.className = "tz-select-option";
      li.setAttribute("role", "option");
      li.setAttribute("data-zone", zone[0]);
      if (zone[0] === selected) li.classList.add("is-selected");
      var name = document.createElement("span");
      name.className = "tz-opt-name";
      name.textContent = zone[0];
      var off = document.createElement("span");
      off.className = "tz-opt-offset";
      off.textContent = tzOffsetLabel(zone[1]);
      li.appendChild(name);
      li.appendChild(off);
      li.addEventListener("click", function () {
        selectTzZone(zone[0]);
      });
      list.appendChild(li);
    });
  }

  function setTzButtonLabel(zone) {
    var nameEl = document.getElementById("tzSelectName");
    var offEl = document.getElementById("tzSelectOffset");
    if (!nameEl) return;
    var off = "";
    for (var i = 0; i < TZ_ZONES.length; i++) {
      if (TZ_ZONES[i][0] === zone) {
        off = tzOffsetLabel(TZ_ZONES[i][1]);
        break;
      }
    }
    nameEl.textContent = zone || "--";
    if (offEl) offEl.textContent = off;
  }

  function selectTzZone(zone) {
    tzState.current = zone;
    setTzButtonLabel(zone);
    var list = document.getElementById("tzSelectList");
    if (list) {
      Array.prototype.forEach.call(list.children, function (li) {
        li.classList.toggle("is-selected", li.getAttribute("data-zone") === zone);
      });
    }
    closeTzList();
  }

  function openTzList() {
    var list = document.getElementById("tzSelectList");
    var btn = document.getElementById("tzSelectBtn");
    if (!list) return;
    list.classList.add("is-open");
    if (btn) btn.setAttribute("aria-expanded", "true");
    tzState.open = true;
  }

  function closeTzList() {
    var list = document.getElementById("tzSelectList");
    var btn = document.getElementById("tzSelectBtn");
    if (!list) return;
    list.classList.remove("is-open");
    if (btn) btn.setAttribute("aria-expanded", "false");
    tzState.open = false;
  }

  function renderTzView(data) {
    var current = toText(data && data.timezone, "");
    tzState.current = current;
    buildTzList(current);
    setTzButtonLabel(current);
    setBadgeState("tzStatusBadge", current || "--", current ? "status-ok" : "status-soft");
    setTzMessage((data && data.message) || t("settings.tzNote"));
    setTzControlsDisabled(tzState.applying);
  }

  function setTzMessage(text) {
    setText("tzMessage", text || t("settings.tzNote"));
  }

  function setTzControlsDisabled(disabled) {
    var ids = ["tzApplyBtn", "tzSelectBtn"];
    ids.forEach(function (id) {
      var el = document.getElementById(id);
      if (el) el.disabled = !!disabled;
    });
  }

  function loadTimezone() {
    if (tzState.loading) return Promise.resolve(null);
    tzState.loading = true;
    return tzRequest("get")
      .then(function (payload) {
        renderTzView(payload || {});
        return payload;
      })
      .catch(function () {
        renderTzView({});
        setTzMessage("Timezone settings are unavailable right now.");
        return null;
      })
      .then(function (result) {
        tzState.loading = false;
        return result;
      });
  }

  function applyTimezone() {
    var zone = tzState.current;
    if (!zone) {
      setTzMessage("Please choose a timezone.");
      return Promise.resolve(null);
    }
    tzState.applying = true;
    setTzControlsDisabled(true);
    setTzMessage("Applying timezone...");
    return tzRequest("apply", { method: "POST", body: "timezone=" + encodeURIComponent(zone) })
      .then(function (payload) {
        tzState.applying = false;
        setTzControlsDisabled(false);
        if (!payload || payload.ok === false) {
          setTzMessage((payload && payload.error) ? payload.error : "Failed to apply timezone.");
          return null;
        }
        tzState.current = toText(payload.timezone, zone);
        setBadgeState("tzStatusBadge", tzState.current, "status-ok");
        setTzMessage("Timezone updated to " + tzState.current + ".");
        return payload;
      })
      .catch(function () {
        tzState.applying = false;
        setTzControlsDisabled(false);
        setTzMessage("Failed to apply timezone.");
        return null;
      });
  }

  function bindTzControls() {
    var applyBtn = document.getElementById("tzApplyBtn");
    if (applyBtn) {
      applyBtn.addEventListener("click", function () {
        applyTimezone();
      });
    }
    var btn = document.getElementById("tzSelectBtn");
    if (btn) {
      btn.addEventListener("click", function (e) {
        e.stopPropagation();
        if (tzState.open) closeTzList();
        else openTzList();
      });
    }
    document.addEventListener("click", function (e) {
      var wrap = document.getElementById("tzSelectWrap");
      if (wrap && tzState.open && !wrap.contains(e.target)) closeTzList();
    });
  }

  function bindWifiControls() {
    var applyBtn = document.getElementById("wifiApplyBtn");
    if (applyBtn) {
      applyBtn.addEventListener("click", function () {
        applyWifiProfile();
      });
    }

    // Password show/hide toggle.
    var passInput = document.getElementById("wifiPassword");
    var toggleBtn = document.getElementById("wifiPasswordToggle");
    if (passInput && toggleBtn) {
      var eyeOn = toggleBtn.querySelector(".eye-icon");
      var eyeOff = toggleBtn.querySelector(".eye-off-icon");
      toggleBtn.addEventListener("click", function () {
        var showing = passInput.type === "text";
        passInput.type = showing ? "password" : "text";
        toggleBtn.classList.toggle("is-visible", !showing);
        toggleBtn.setAttribute("aria-label", showing ? "Show password" : "Hide password");
        if (eyeOn) eyeOn.style.display = showing ? "" : "none";
        if (eyeOff) eyeOff.style.display = showing ? "none" : "";
      });
    }

    // Auto-fill the password when a remembered SSID is picked from the datalist.
    var ssidInput = document.getElementById("wifiSsid");
    if (ssidInput && passInput) {
      ssidInput.addEventListener("change", function () {
        var chosen = ssidInput.value.trim();
        if (!chosen) return;
        var match = loadWifiHistory().filter(function (item) {
          return item && item.ssid === chosen;
        })[0];
        if (match && match.pass) {
          passInput.value = match.pass;
        }
      });
    }
  }

  /* ── OTA update check + progress overlay ─────────────────
   * The browser polls the board-cached manifest every 60s (the board proxies
   * the OTA repo because the release host chain sends no CORS headers).
   * Apply progress comes from /run/rockbase/ota-state.json, the same state
   * file the LCD full-screen overlay consumes. */

  var OTA_CHECK_SECONDS = 60;
  var OTA_PROGRESS_SECONDS = 2;
  var OTA_REBOOT_WATCH_SECONDS = 5;
  var OTA_REMIND_DELAY_MS = 24 * 60 * 60 * 1000;
  var otaCheckTimer = null;
  var otaProgressTimer = null;
  var otaRebootTimer = null;
  var otaApplying = false;
  var otaOfferedVersion = "";
  var otaApplyStartTs = 0;
  var otaLastStateTs = 0;
  var otaStallSince = 0;
  var otaStateFailCount = 0;
  var OTA_STATE_FAIL_REBOOT_THRESHOLD = 3;
  var OTA_STALL_SECONDS = 90;
  var OTA_AUTO_REBOOT_DELAY_MS = 3000;
  var otaAutoRebootTimer = null;
  var otaRebootTriggered = false;
  var currentProductVersion = "--";

  var OTA_PHASE_KEYS = ["preparing", "downloading", "verifying", "flashing", "reboot_pending", "switching", "failed"];

  function otaPhaseLabel(phase) {
    var key = "ota.phase." + phase;
    return t(key) === key ? (phase || "") : t(key);
  }

  function otaVersionGt(a, b) {
    var pa = String(a || "").replace(/^v/, "").split("-")[0].split(".");
    var pb = String(b || "").replace(/^v/, "").split("-")[0].split(".");
    for (var i = 0; i < 3; i += 1) {
      var na = Number(pa[i]) || 0;
      var nb = Number(pb[i]) || 0;
      if (na > nb) return true;
      if (na < nb) return false;
    }
    return false;
  }

  function otaDismissRecord() {
    try {
      var raw = window.localStorage.getItem("nmOtaDismissed") || "";
      if (!raw) return null;
      try {
        var parsed = JSON.parse(raw);
        if (parsed && typeof parsed === "object" && typeof parsed.version === "string") {
          return parsed;
        }
      } catch (e) {}
      return { version: String(raw), ts: 0 };
    } catch (e) {
      return null;
    }
  }

  function otaDismissedVersion() {
    var record = otaDismissRecord();
    if (!record) return "";
    return record.version || "";
  }

  function otaShouldSuppress(version) {
    var record = otaDismissRecord();
    if (!record || record.version !== version) return false;
    var ts = Number(record.ts) || 0;
    if (!ts) return false;
    return Date.now() - ts < OTA_REMIND_DELAY_MS;
  }

  function otaDismiss(version) {
    try {
      window.localStorage.setItem("nmOtaDismissed", JSON.stringify({ version: version, ts: Date.now() }));
    } catch (e) {}
  }

  function otaClearDismiss() {
    try {
      window.localStorage.removeItem("nmOtaDismissed");
    } catch (e) {}
  }

  function otaShowView(view) {
    var overlay = document.getElementById("otaOverlay");
    var offer = document.getElementById("otaOfferView");
    var progress = document.getElementById("otaProgressView");
    var confirm = document.getElementById("otaConfirmView");
    if (!overlay || !offer || !progress) return;
    overlay.hidden = view === null;
    offer.hidden = view !== "offer";
    progress.hidden = view !== "progress";
    if (confirm) confirm.hidden = view !== "confirm";
  }

  function otaFormatSpeed(bps) {
    var num = Number(bps);
    if (!Number.isFinite(num) || num < 0) return "";
    if (num >= 1048576) return (num / 1048576).toFixed(1) + " MB/s";
    if (num >= 1024) return Math.round(num / 1024) + " KB/s";
    return Math.round(num) + " B/s";
  }

  function otaSetProgress(pct) {
    var fill = document.getElementById("otaProgressFill");
    var label = document.getElementById("otaProgressPct");
    if (!fill || !label) return;
    var num = Number(pct);
    if (!Number.isFinite(num) || num < 0) {
      fill.classList.add("is-indeterminate");
      fill.style.width = "";
      label.textContent = "";
      return;
    }
    fill.classList.remove("is-indeterminate");
    fill.style.width = Math.min(100, Math.max(0, num)) + "%";
    label.textContent = Math.round(num) + "%";
  }

  function otaShowOffer(latest, released) {
    if (otaApplying) return;
    otaOfferedVersion = latest;
    setText("otaCurrentVer", currentProductVersion);
    setText("otaLatestVer", latest);
    setText("otaReleaseDate", released ? t("ota.releasedPrefix") + released : "");
    otaShowView("offer");
  }

  function checkOtaUpdate() {
    if (otaApplying) return Promise.resolve(null);
    var overlay = document.getElementById("otaOverlay");
    if (overlay && !overlay.hidden) return Promise.resolve(null);
    return window.fetch("/api/ota.cgi?action=manifest&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (response) {
        return response.ok ? response.json() : null;
      })
      .then(function (manifest) {
        if (!manifest || typeof manifest.version !== "string") return null;
        var latest = manifest.version.split("-")[0];
        if (!/^v\d+\.\d+\.\d+$/.test(latest)) return null;
        if (!/^v\d+\.\d+\.\d+$/.test(currentProductVersion)) return null;
        if (!otaVersionGt(latest, currentProductVersion)) return null;
        if (otaShouldSuppress(latest)) return null;
        otaShowOffer(latest, manifest.released);
        return manifest;
      })
      .catch(function () {
        return null;
      });
  }

  function startOtaChecking() {
    if (otaCheckTimer) window.clearInterval(otaCheckTimer);
    otaCheckTimer = window.setInterval(checkOtaUpdate, OTA_CHECK_SECONDS * 1000);
    window.setTimeout(checkOtaUpdate, 8000);
  }

  function stopOtaProgressPolling() {
    if (otaProgressTimer) {
      window.clearInterval(otaProgressTimer);
      otaProgressTimer = null;
    }
  }

  function stopOtaRebootWatch() {
    if (otaRebootTimer) {
      window.clearInterval(otaRebootTimer);
      otaRebootTimer = null;
    }
  }

  function otaShowProgress() {
    if (otaAutoRebootTimer) { window.clearTimeout(otaAutoRebootTimer); otaAutoRebootTimer = null; }
    otaRebootTriggered = false;
    setText("otaPhaseLabel", otaPhaseLabel("preparing"));
    setText("otaProgressSpeed", "");
    setText("otaProgressMsg", t("ota.doNotPowerOff"));
    otaSetProgress(-1);
    var errActions = document.getElementById("otaErrorActions");
    if (errActions) errActions.hidden = true;
    var rebootActions = document.getElementById("otaRebootActions");
    if (rebootActions) rebootActions.hidden = true;
    otaShowView("progress");
  }

  function otaShowRebootPending() {
    setText("otaPhaseLabel", otaPhaseLabel("reboot_pending"));
    setText("otaProgressSpeed", "");
    setText("otaProgressMsg", t("ota.rebootAuto"));
    otaSetProgress(100);
    var rebootActions = document.getElementById("otaRebootActions");
    if (rebootActions) rebootActions.hidden = false;
    /* Auto-trigger the reboot once the 100% write-complete state is confirmed;
       the UI only shows a "rebooting automatically" waiting indicator. */
    if (otaAutoRebootTimer) window.clearTimeout(otaAutoRebootTimer);
    otaAutoRebootTimer = window.setTimeout(otaConfirmReboot, OTA_AUTO_REBOOT_DELAY_MS);
  }

  function otaShowFailed(message) {
    otaApplying = false;
    if (otaAutoRebootTimer) { window.clearTimeout(otaAutoRebootTimer); otaAutoRebootTimer = null; }
    otaRebootTriggered = false;
    stopOtaProgressPolling();
    setText("otaPhaseLabel", otaPhaseLabel("failed"));
    setText("otaProgressMsg", message || t("ota.errorDefault"));
    otaSetProgress(0);
    var errActions = document.getElementById("otaErrorActions");
    if (errActions) errActions.hidden = false;
  }

  function otaRenderState(state) {
    var phase = toText(state && state.phase, "preparing");
    setText("otaPhaseLabel", otaPhaseLabel(phase));
    setText("otaProgressSpeed", otaFormatSpeed(state && state.speed_bps));
    var msg = toText(state && state.message, "");
    if (msg && phase !== "failed") setText("otaProgressMsg", msg);
    otaSetProgress(state ? state.progress : -1);

    if (phase === "failed") {
      otaShowFailed(msg);
      return;
    }
    if (phase === "reboot_pending") {
      stopOtaProgressPolling();
      otaShowRebootPending();
      return;
    }
    if (phase === "switching") {
      stopOtaProgressPolling();
      otaStartRebootWatch();
    }
  }

  function pollOtaState() {
    return window.fetch("/api/ota.cgi?action=state&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      })
      .then(function (res) {
        otaStateFailCount = 0;
        if (!res || !res.ok) return null;
        var state = res.state;
        if (!state) return res;
        var stateTs = Number(state.ts) || 0;
        /* state file survives a finished run; ignore entries older than this apply */
        if (otaApplyStartTs && stateTs && stateTs < otaApplyStartTs) return res;
        var now = Math.floor(Date.now() / 1000);
        if (stateTs !== otaLastStateTs) {
          otaLastStateTs = stateTs;
          otaStallSince = 0;
        } else if (!otaStallSince) {
          otaStallSince = now;
        } else if (now - otaStallSince > OTA_STALL_SECONDS && state.phase !== "switching" && state.phase !== "reboot_pending") {
          otaShowFailed("No progress from the node for over 90s - the update may have stalled. Check /root/config/ota/ota.log.");
          return res;
        }
        otaRenderState(state);
        return res;
      })
      .catch(function () {
        /* httpd is kept alive during the whole apply; if it stops answering
           mid-apply the node is almost certainly rebooting into the new slot
           (the switching state window is short and easy to miss between polls) */
        if (!otaApplying) return null;
        otaStateFailCount += 1;
        if (otaStateFailCount >= OTA_STATE_FAIL_REBOOT_THRESHOLD) {
          stopOtaProgressPolling();
          otaStartRebootWatch();
        }
        return null;
      });
  }

  function startOtaProgressPolling() {
    stopOtaProgressPolling();
    otaProgressTimer = window.setInterval(pollOtaState, OTA_PROGRESS_SECONDS * 1000);
    pollOtaState();
  }

  function otaStartRebootWatch() {
    stopOtaRebootWatch();
    otaStateFailCount = 0;
    otaSetProgress(100);
    setText("otaPhaseLabel", otaPhaseLabel("switching"));
    setText("otaProgressSpeed", "");
    setText("otaProgressMsg", t("ota.rebootMsg"));
    otaRebootTimer = window.setInterval(function () {
      window.fetch("/api/status.cgi?_ts=" + Date.now(), { cache: "no-store" })
        .then(function (response) {
          if (!response.ok) throw new Error("not back yet");
          return response.json();
        })
        .then(function () {
          stopOtaRebootWatch();
          otaApplying = false;
          otaClearDismiss();
          setText("otaPhaseLabel", t("ota.updateComplete"));
          setText("otaProgressMsg", t("ota.reloading"));
          window.setTimeout(function () {
            window.location.reload();
          }, 1500);
        })
        .catch(function () {});
    }, OTA_REBOOT_WATCH_SECONDS * 1000);
  }

  function startOtaApply() {
    return window.fetch("/api/ota.cgi?action=apply&_ts=" + Date.now(), {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "",
      cache: "no-store"
    })
      .then(function (response) {
        return response.ok ? response.json() : null;
      })
      .then(function (res) {
        if (res && res.ok) {
          otaApplying = true;
          otaApplyStartTs = Math.floor(Date.now() / 1000);
          otaLastStateTs = 0;
          otaStallSince = 0;
          otaStateFailCount = 0;
          otaShowProgress();
          startOtaProgressPolling();
          return res;
        }
        if (res && res.applying) {
          otaApplying = true;
          otaShowProgress();
          startOtaProgressPolling();
          return res;
        }
        otaShowView("progress");
        otaShowFailed(res && res.error ? res.error : "failed to start update");
        return null;
      })
      .catch(function () {
        otaShowView("progress");
        otaShowFailed("failed to reach the node");
        return null;
      });
  }

  function otaConfirmReboot() {
    if (otaRebootTriggered) return;
    otaRebootTriggered = true;
    if (otaAutoRebootTimer) { window.clearTimeout(otaAutoRebootTimer); otaAutoRebootTimer = null; }
    setText("otaPhaseLabel", otaPhaseLabel("switching"));
    setText("otaProgressMsg", t("ota.rebootSent"));
    window.fetch("/api/ota.cgi?action=reboot&_ts=" + Date.now(), {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "",
      cache: "no-store"
    }).catch(function () {
      return null;
    }).then(function () {
      /* whether or not the response made it out before the network went down,
         the node is on its way down - just watch for it to come back */
      otaStartRebootWatch();
    });
  }

  function resumeOtaIfApplying() {
    window.fetch("/api/ota.cgi?action=state&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (response) {
        return response.ok ? response.json() : null;
      })
      .then(function (res) {
        if (!res || !res.ok) return;
        if (res.applying || (res.state && res.state.phase && res.state.phase !== "failed")) {
          otaApplying = true;
          otaShowProgress();
          startOtaProgressPolling();
        }
      })
      .catch(function () {});
  }

  /* ── About page: firmware update check ─────────────────── */

  function aboutCheckUpdate() {
    var badge = document.getElementById("aboutUpdateBadge");
    var note = document.getElementById("aboutUpdateNote");
    var updateBtn = document.getElementById("aboutUpdateBtn");
    var forceBtn = document.getElementById("aboutForceBtn");
    var checkBtn = document.getElementById("aboutCheckBtn");

    if (checkBtn) checkBtn.disabled = true;
    if (badge) { badge.textContent = t("about.checking"); badge.className = "status-badge status-soft"; }
    if (note) note.textContent = t("about.contacting");

    return window.fetch("/api/ota.cgi?action=manifest&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (response) {
        return response.ok ? response.json() : null;
      })
      .then(function (manifest) {
        if (checkBtn) checkBtn.disabled = false;
        if (!manifest || typeof manifest.version !== "string") {
          if (badge) { badge.textContent = t("about.unavailable"); badge.className = "status-badge status-danger"; }
          if (note) note.textContent = t("about.unreachable");
          return;
        }

        var latest = manifest.version.split("-")[0];
        var current = currentProductVersion;
        setText("aboutLatestVer", latest);
        setText("aboutReleased", manifest.released || "--");

        if (!/^v\d+\.\d+\.\d+$/.test(latest) || !/^v\d+\.\d+\.\d+$/.test(current)) {
          if (badge) { badge.textContent = t("about.unknown"); badge.className = "status-badge"; }
          if (note) note.textContent = t("about.unknownFormat");
          return;
        }

        if (otaVersionGt(latest, current)) {
          if (badge) { badge.textContent = t("about.updateAvailable"); badge.className = "status-badge status-warm"; }
          if (note) note.textContent = t("about.updateAvailableNote");
          if (updateBtn) updateBtn.hidden = false;
          if (forceBtn) forceBtn.hidden = true;
        } else if (otaVersionGt(current, latest)) {
          if (badge) { badge.textContent = t("about.upToDate"); badge.className = "status-badge status-ok"; }
          if (note) note.textContent = t("about.upToDateNewer");
          if (updateBtn) updateBtn.hidden = true;
          if (forceBtn) forceBtn.hidden = false;
        } else {
          if (badge) { badge.textContent = t("about.upToDate"); badge.className = "status-badge status-ok"; }
          if (note) note.textContent = t("about.upToDateLatest");
          if (updateBtn) updateBtn.hidden = true;
          if (forceBtn) forceBtn.hidden = false;
        }
      })
      .catch(function () {
        if (checkBtn) checkBtn.disabled = false;
        if (badge) { badge.textContent = t("about.error"); badge.className = "status-badge status-danger"; }
        if (note) note.textContent = t("about.checkFailed");
      });
  }

  function bindOtaControls() {
    var later = document.getElementById("otaLaterBtn");
    var update = document.getElementById("otaUpdateBtn");
    var close = document.getElementById("otaCloseBtn");
    if (later) {
      later.addEventListener("click", function () {
        if (otaOfferedVersion) otaDismiss(otaOfferedVersion);
        otaShowView(null);
      });
    }
    if (update) {
      update.addEventListener("click", function () {
        startOtaApply();
      });
    }
    if (close) {
      close.addEventListener("click", function () {
        otaShowView(null);
      });
    }
    var forceCancel = document.getElementById("otaForceCancelBtn");
    var forceConfirm = document.getElementById("otaForceConfirmBtn");
    if (forceCancel) {
      forceCancel.addEventListener("click", function () {
        otaShowView(null);
      });
    }
    if (forceConfirm) {
      forceConfirm.addEventListener("click", function () {
        forceConfirm.disabled = true;
        startOtaApply();
      });
    }

    /* About page update buttons */
    var aboutCheck = document.getElementById("aboutCheckBtn");
    var aboutUpdate = document.getElementById("aboutUpdateBtn");
    var aboutForce = document.getElementById("aboutForceBtn");
    if (aboutCheck) {
      aboutCheck.addEventListener("click", function () {
        aboutCheckUpdate();
      });
    }
    if (aboutUpdate) {
      aboutUpdate.addEventListener("click", function () {
        otaClearDismiss();
        startOtaApply();
      });
    }
    if (aboutForce) {
      aboutForce.addEventListener("click", function () {
        otaShowForceConfirm();
      });
    }
  }

  function otaShowForceConfirm() {
    if (otaApplying) return;
    var btn = document.getElementById("otaForceConfirmBtn");
    if (btn) btn.disabled = false;
    var ver = currentProductVersion || "--";
    setText("otaForceVer", ver);
    var note = document.getElementById("otaForceNote");
    if (note) note.innerHTML = t("ota.forceNote").replace("{ver}", '<span class="mono">' + escapeHtml(ver) + "</span>");
    otaShowView("confirm");
  }

  /* ── i18n ───────────────────────────────────────────────
   * Wires the existing (previously inert) #langSelect. Default is English;
   * choice persists in localStorage. Only the strings tagged with [data-i18n]
   * are translated; everything else stays as authored. */
  var I18N = {
    en: {
      /* nav / topbar */
      "nav.dashboard": "Dashboard",
      "nav.miners": "Miners",
      "nav.logs": "Logs",
      "nav.settings": "Settings",
      "nav.about": "About",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "Uptime",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Solo BTC Node on Rockbase",
      /* dashboard */
      "dash.loading": "Loading live node data...",
      "dash.peers": "Peers",
      "dash.difficulty": "Difficulty",
      "dash.utxo": "UTXO Set",
      /* stratum card */
      "stratum.endpoint": "Endpoint",
      "stratum.hashrate": "Pool Hashrate 5m",
      "stratum.bestShare": "Best Share",
      "stratum.workersMiners": "Workers / Miners",
      /* system & network */
      "sys.title": "System & Network",
      "sys.cpu": "CPU Usage",
      "sys.blockData": "Block Data",
      "sys.ram": "RAM",
      "sys.swap": "Swap",
      "sys.temp": "Temperature",
      "sys.soc": "SoC",
      "sys.rootfs": "Root FS",
      "sys.ipMac": "IP & MAC",
      "sys.wifiRssi": "WiFi & RSSI",
      /* miners page */
      "miners.summary": "Summary",
      "miners.miners": "Miners",
      "miners.workers": "Workers",
      "miners.totalHash": "Total Hashrate 5m",
      "miners.bestShareEver": "Best Share Ever",
      "miners.waiting": "Waiting for ckpool cache...",
      "miners.hashHistory": "Hashrate History",
      "miners.noWorkers": "No workers.",
      "miners.noMiners": "No miners found yet. Connect a miner to the local Stratum endpoint.",
      /* miner detail */
      "miner.back": "\u2190 Back to Miners",
      "miner.hash5m": "Hashrate (5m)",
      "miner.totalShares": "Total Shares",
      "miner.bestEver": "Best Ever",
      "miner.blockOdds": "Block Odds",
      "miner.1d": "1 Day",
      "miner.1w": "1 Week",
      "miner.1m": "1 Month",
      "miner.1y": "1 Year",
      "miner.workerHistory": "Worker History",
      "miner.noData": "No Data",
      "miner.online": "\u25cf Online",
      "miner.offline": "\u25cf Offline",
      "miner.authorised": "Authorised",
      "miner.notAuthorised": "Not authorised",
      "miner.lastShare": "Last share",
      "miner.worker": "worker",
      "miner.workers": "workers",
      /* logs */
      "logs.summary": "Tail 300 lines \u00b7 live cursor",
      "logs.loading": "Loading logs...",
      "logs.polling": "Polling live updates every 5s",
      "logs.empty": "[empty]",
      /* settings */
      "settings.network": "Network",
      "settings.wifi": "Wi-Fi Setup",
      "settings.ssid": "SSID",
      "settings.password": "Password",
      "settings.wifiNote": "Enter your Wi-Fi SSID and password, then tap Apply. The device will reconnect automatically.",
      "settings.timezone": "Timezone",
      "settings.tzNote": "Select a timezone and tap Apply. Takes effect immediately.",
      /* about */
      "about.subtitle": "Versioning, firmware updates, and support diagnostics.",
      "about.firmwareUpdate": "Firmware Update",
      "about.current": "Current",
      "about.latest": "Latest",
      "about.released": "Released",
      "about.checkUpdates": "Check for Updates",
      "about.updateNow": "Update Now",
      "about.forceUpdate": "Force Update",
      "about.firmware": "Firmware",
      "about.board": "Board",
      "about.api": "API",
      "about.system": "System",
      "about.productVersion": "Product Version",
      "about.lastUpdated": "Last Updated",
      "about.checking": "Checking\u2026",
      "about.contacting": "Contacting update server\u2026",
      "about.unavailable": "Unavailable",
      "about.unreachable": "Could not reach the update server. Try again later.",
      "about.unknown": "Unknown",
      "about.unknownFormat": "Version format unrecognized.",
      "about.updateAvailable": "Update available",
      "about.updateAvailableNote": "A newer firmware version is available. Update recommended.",
      "about.upToDate": "Up to date",
      "about.upToDateNewer": "You are running a newer version than the latest release.",
      "about.upToDateLatest": "You are running the latest firmware version.",
      "about.error": "Error",
      "about.checkFailed": "Failed to check for updates.",
      /* ota */
      "ota.available": "Firmware Update Available",
      "ota.note": "The update is downloaded, verified and written to the standby system slot. Syncing and mining pause during the upgrade. When writing finishes the node reboots automatically. Do not power off.",
      "ota.remindTomorrow": "Remind me tomorrow",
      "ota.updating": "Updating Firmware",
      "ota.rebootWait": "Rebooting automatically \u2014 please wait, do not power off.",
      "ota.doNotPowerOff": "Do not power off the node.",
      "ota.rebootAuto": "The new system is written and verified. Rebooting automatically in a few seconds...",
      "ota.rebootMsg": "The node is rebooting. This page reloads when it is back.",
      "ota.updateComplete": "Update complete",
      "ota.reloading": "Reloading...",
      "ota.rebootSent": "Reboot command sent. The node is restarting, this page reloads when it is back.",
      "ota.errorDefault": "See /root/config/ota/ota.log on the node for details.",
      "ota.releasedPrefix": "Released ",
      "ota.forceNote": "You are about to force-install firmware {ver}. This will overwrite the current system slot even if the version is the same or newer. Syncing and mining will pause during the upgrade. Do not power off.",
      "ota.phase.preparing": "Preparing...",
      "ota.phase.downloading": "Downloading firmware",
      "ota.phase.verifying": "Verifying signature & checksum",
      "ota.phase.flashing": "Writing system image",
      "ota.phase.reboot_pending": "Write complete",
      "ota.phase.switching": "Switching to new system, rebooting",
      "ota.phase.failed": "Update failed",
      /* status labels */
      "label.sync.syncing": "IBD Syncing",
      "label.sync.pre_headers": "Pre-sync blockheaders",
      "label.sync.sync_headers": "Synchronizing blockheaders",
      "label.sync.sync_blocks": "Synchronizing blocks",
      "label.sync.loading_index": "Loading block index",
      "label.sync.loading_blocks": "Loading block files",
      "label.sync.verifying_blocks": "Verifying blocks",
      "label.sync.rewinding_blocks": "Rewinding blocks",
      "label.sync.pruning": "Pruning blocks",
      "label.sync.synced": "Synced",
      "label.sync.btc_not_running": "Not Running",
      "label.prune.pruned_mode": "Pruned Node",
      "label.prune.full_mode": "Full Node",
      "label.stratum.running": "Running",
      "label.stratum.stopped": "Stopped",
      "wifi.idle": "Idle",
      "theme.dark": "Dark",
      "theme.light": "Light",
      /* common */
      "common.loading": "Loading...",
      "common.apply": "Apply",
      /* clean block */
      "cleanBlock.btn": "Clean Block Data",
      "cleanBlock.title": "Clean Block Data",
      "cleanBlock.warn": "This will completely delete the block data already synced on this device. After deletion the node re-syncs all blocks from scratch, which is expected to take more than 1 month. Mining pauses during the operation. Do not power off.",
      "cleanBlock.cancel": "Cancel",
      "cleanBlock.confirm": "Clean Now",
      "cleanBlock.progressTitle": "Cleaning Block Data",
      "cleanBlock.progressNote": "Do not power off the node.",
      "cleanBlock.close": "Close",
      "cleanBlock.phase.stopping": "Stopping bitcoind...",
      "cleanBlock.phase.cleaning": "Wiping block data...",
      "cleanBlock.phase.restarting": "Restarting bitcoind...",
      "cleanBlock.phase.done": "Done. Re-syncing all blocks (expected more than 1 month).",
      "cleanBlock.phase.error": "Clean finished with an error. Check the logs."
    },
    zh: {
      /* nav / topbar */
      "nav.dashboard": "仪表盘",
      "nav.miners": "矿机",
      "nav.logs": "日志",
      "nav.settings": "设置",
      "nav.about": "关于",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "运行时间",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Rockbase 独立比特币节点",
      /* dashboard */
      "dash.loading": "正在加载节点实时数据...",
      "dash.peers": "对等节点",
      "dash.difficulty": "难度",
      "dash.utxo": "UTXO 集合",
      /* stratum card */
      "stratum.endpoint": "接入地址",
      "stratum.hashrate": "池算力 5m",
      "stratum.bestShare": "最佳 Share",
      "stratum.workersMiners": "Workers / 矿机",
      /* system & network */
      "sys.title": "系统与网络",
      "sys.cpu": "CPU 占用",
      "sys.blockData": "区块数据",
      "sys.ram": "内存",
      "sys.swap": "交换分区",
      "sys.temp": "温度",
      "sys.soc": "SoC",
      "sys.rootfs": "根文件系统",
      "sys.ipMac": "IP 与 MAC",
      "sys.wifiRssi": "WiFi 与信号",
      /* miners page */
      "miners.summary": "概览",
      "miners.miners": "矿机",
      "miners.workers": "Workers",
      "miners.totalHash": "总算力 5m",
      "miners.bestShareEver": "历史最佳 Share",
      "miners.waiting": "等待 ckpool 缓存...",
      "miners.hashHistory": "算力历史",
      "miners.noWorkers": "暂无 worker。",
      "miners.noMiners": "尚未发现矿机。请将矿机连接到本地 Stratum 接入地址。",
      /* miner detail */
      "miner.back": "\u2190 返回矿机列表",
      "miner.hash5m": "算力 (5m)",
      "miner.totalShares": "总 Shares",
      "miner.bestEver": "历史最佳",
      "miner.blockOdds": "出块概率",
      "miner.1d": "1 天",
      "miner.1w": "1 周",
      "miner.1m": "1 个月",
      "miner.1y": "1 年",
      "miner.workerHistory": "Worker 历史",
      "miner.noData": "无数据",
      "miner.online": "\u25cf 在线",
      "miner.offline": "\u25cf 离线",
      "miner.authorised": "已授权",
      "miner.notAuthorised": "未授权",
      "miner.lastShare": "最近 Share",
      "miner.worker": "个 worker",
      "miner.workers": "个 worker",
      /* logs */
      "logs.summary": "尾部 300 行 · 实时游标",
      "logs.loading": "正在加载日志...",
      "logs.polling": "每 5 秒轮询实时更新",
      "logs.empty": "[空]",
      /* settings */
      "settings.network": "网络",
      "settings.wifi": "Wi-Fi 设置",
      "settings.ssid": "SSID",
      "settings.password": "密码",
      "settings.wifiNote": "输入 Wi-Fi 的 SSID 和密码，然后点击应用。设备将自动重连。",
      "settings.timezone": "时区",
      "settings.tzNote": "选择时区后点击应用，立即生效。",
      /* about */
      "about.subtitle": "版本管理、固件更新与支持诊断。",
      "about.firmwareUpdate": "固件更新",
      "about.current": "当前版本",
      "about.latest": "最新版本",
      "about.released": "发布日期",
      "about.checkUpdates": "检查更新",
      "about.updateNow": "立即更新",
      "about.forceUpdate": "强制更新",
      "about.firmware": "固件",
      "about.board": "主板",
      "about.api": "API",
      "about.system": "系统",
      "about.productVersion": "产品版本",
      "about.lastUpdated": "最近更新",
      "about.checking": "检查中…",
      "about.contacting": "正在连接更新服务器…",
      "about.unavailable": "不可用",
      "about.unreachable": "无法连接更新服务器，请稍后再试。",
      "about.unknown": "未知",
      "about.unknownFormat": "无法识别的版本格式。",
      "about.updateAvailable": "有可用更新",
      "about.updateAvailableNote": "有更新的固件版本可用，建议更新。",
      "about.upToDate": "已是最新",
      "about.upToDateNewer": "你运行的版本比最新发布版更新。",
      "about.upToDateLatest": "你正在运行最新的固件版本。",
      "about.error": "错误",
      "about.checkFailed": "检查更新失败。",
      /* ota */
      "ota.available": "有可用固件更新",
      "ota.note": "更新包将被下载、校验并写入备用系统槽位。升级期间同步和挖矿会暂停。写入完成后节点会自动重启。请勿断电。",
      "ota.remindTomorrow": "明天提醒我",
      "ota.updating": "正在更新固件",
      "ota.rebootWait": "正在自动重启 — 请稍候，请勿断电。",
      "ota.doNotPowerOff": "请勿给节点断电。",
      "ota.rebootAuto": "新系统已写入并校验完成，几秒后将自动重启...",
      "ota.rebootMsg": "节点正在重启，恢复后本页面会自动刷新。",
      "ota.updateComplete": "更新完成",
      "ota.reloading": "正在刷新...",
      "ota.rebootSent": "重启指令已发送，节点正在重启，恢复后本页面会自动刷新。",
      "ota.errorDefault": "详情请查看节点上的 /root/config/ota/ota.log。",
      "ota.releasedPrefix": "发布于 ",
      "ota.forceNote": "你即将强制安装固件 {ver}。即使版本相同或更旧，也会覆盖当前系统槽位。升级期间同步和挖矿会暂停。请勿断电。",
      "ota.phase.preparing": "准备中...",
      "ota.phase.downloading": "正在下载固件",
      "ota.phase.verifying": "正在校验签名与校验和",
      "ota.phase.flashing": "正在写入系统镜像",
      "ota.phase.reboot_pending": "写入完成",
      "ota.phase.switching": "正在切换到新系统并重启",
      "ota.phase.failed": "更新失败",
      /* status labels */
      "label.sync.syncing": "IBD 同步中",
      "label.sync.pre_headers": "预同步区块头",
      "label.sync.sync_headers": "正在同步区块头",
      "label.sync.sync_blocks": "正在同步区块",
      "label.sync.loading_index": "正在加载区块索引",
      "label.sync.loading_blocks": "正在加载区块文件",
      "label.sync.verifying_blocks": "正在校验区块",
      "label.sync.rewinding_blocks": "正在回滚区块",
      "label.sync.pruning": "正在剪枝区块",
      "label.sync.synced": "已同步",
      "label.sync.btc_not_running": "未运行",
      "label.prune.pruned_mode": "剪枝节点",
      "label.prune.full_mode": "完整节点",
      "label.stratum.running": "运行中",
      "label.stratum.stopped": "已停止",
      "wifi.idle": "空闲",
      "theme.dark": "深色",
      "theme.light": "浅色",
      /* common */
      "common.loading": "加载中...",
      "common.apply": "应用",
      /* clean block */
      "cleanBlock.btn": "清理区块数据",
      "cleanBlock.title": "清理区块数据",
      "cleanBlock.warn": "这将完全删除设备上已同步的区块数据。删除后节点将从零重新同步所有区块，预计耗时 1 个月以上。期间挖矿暂停，请勿断电。",
      "cleanBlock.cancel": "取消",
      "cleanBlock.confirm": "立即清理",
      "cleanBlock.progressTitle": "正在清理区块数据",
      "cleanBlock.progressNote": "请勿断电。",
      "cleanBlock.close": "关闭",
      "cleanBlock.phase.stopping": "正在停止 bitcoind...",
      "cleanBlock.phase.cleaning": "正在清空区块数据...",
      "cleanBlock.phase.restarting": "正在重启 bitcoind...",
      "cleanBlock.phase.done": "清理完成，正在重新同步所有区块（预计 1 个月以上）。",
      "cleanBlock.phase.error": "清理出现错误，请查看日志。"
    }
  };

  /* Supported language codes. Only "en" and "zh" have translations today;
   * every other code is selectable but falls back to English strings (the
   * t() helper does this automatically). Adding a language later only means
   * adding an I18N[<code>] dictionary — no UI changes needed. */
  var LANG_CODES = (function () {
    var sel = document.getElementById("langSelect");
    if (!sel) return ["en", "zh"];
    var codes = [];
    for (var i = 0; i < sel.options.length; i++) codes.push(sel.options[i].value);
    return codes.length ? codes : ["en", "zh"];
  })();

  function isKnownLang(code) {
    return LANG_CODES.indexOf(code) !== -1;
  }

  function getStoredLang() {
    try {
      var stored = window.localStorage.getItem("nmLang");
      return isKnownLang(stored) ? stored : "en";
    } catch (e) {
      return "en";
    }
  }

  var currentLang = getStoredLang();

  function t(key) {
    var dict = I18N[currentLang] || I18N.en;
    if (dict[key] != null) return dict[key];
    if (I18N.en[key] != null) return I18N.en[key];
    return key;
  }

  function applyI18n() {
    var nodes = document.querySelectorAll("[data-i18n]");
    nodes.forEach(function (n) {
      var key = n.getAttribute("data-i18n");
      var val = t(key);
      if (val != null) n.textContent = val;
    });
    /* theme toggle label is set dynamically (not via data-i18n) */
    var themeLabel = document.getElementById("themeToggleLabel");
    if (themeLabel) {
      var curTheme = document.documentElement.getAttribute("data-theme") === "light" ? "light" : "dark";
      themeLabel.textContent = t("theme." + curTheme);
    }
    document.documentElement.setAttribute("lang", currentLang);
  }

  function setLang(lang) {
    currentLang = isKnownLang(lang) ? lang : "en";
    try {
      window.localStorage.setItem("nmLang", currentLang);
    } catch (e) {}
    applyI18n();
    /* re-normalize + re-render dashboard so dynamic status badges (Synced/Running/...)
     * follow the language — they are computed from raw API data at normalize time */
    if (lastDashboardApi) renderDashboard(normalizeDashboardData(lastDashboardApi));
    if (cleanBlockActive) updateCleanBlockPhaseLabel();
  }

  function bindLangSelect() {
    var sel = document.getElementById("langSelect");
    if (!sel) return;
    sel.value = currentLang;
    sel.addEventListener("change", function () {
      setLang(sel.value);
    });
  }

  /* ── Clean block data ───────────────────────────────────
   * Button on the Bitcoind card -> confirm dialog -> POST /api/cleanblock.cgi
   * -> poll state until done/error. The heavy work runs server-side async. */
  var cleanBlockActive = false;
  var cleanBlockPollTimer = null;

  function cleanBlockShowView(view) {
    var overlay = document.getElementById("cleanBlockOverlay");
    var confirm = document.getElementById("cleanBlockConfirmView");
    var progress = document.getElementById("cleanBlockProgressView");
    if (!overlay || !confirm || !progress) return;
    overlay.hidden = view === null;
    confirm.hidden = view !== "confirm";
    progress.hidden = view !== "progress";
  }

  function updateCleanBlockPhaseLabel(phase) {
    var el = document.getElementById("cleanBlockPhaseLabel");
    if (!el) return;
    el.textContent = t("cleanBlock.phase." + phase) || phase;
  }

  function stopCleanBlockPolling() {
    if (cleanBlockPollTimer) {
      window.clearInterval(cleanBlockPollTimer);
      cleanBlockPollTimer = null;
    }
  }

  function cleanBlockPollState() {
    return window.fetch("/api/cleanblock.cgi?action=state&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (st) {
        if (!st || !st.phase) return;
        var phase = st.phase;
        updateCleanBlockPhaseLabel(phase);
        if (phase === "done" || phase === "error") {
          stopCleanBlockPolling();
          var doneActions = document.getElementById("cleanBlockDoneActions");
          if (doneActions) doneActions.hidden = false;
          var fill = document.getElementById("cleanBlockProgressFill");
          if (fill) fill.classList.remove("is-indeterminate");
        }
      })
      .catch(function () {});
  }

  function cleanBlockStartPolling() {
    stopCleanBlockPolling();
    cleanBlockPollTimer = window.setInterval(cleanBlockPollState, 3000);
    cleanBlockPollState();
  }

  function cleanBlockTrigger() {
    var confirmBtn = document.getElementById("cleanBlockConfirmBtn");
    if (confirmBtn) confirmBtn.disabled = true;
    cleanBlockActive = true;
    cleanBlockShowView("progress");
    updateCleanBlockPhaseLabel("stopping");
    var doneActions = document.getElementById("cleanBlockDoneActions");
    if (doneActions) doneActions.hidden = true;
    var fill = document.getElementById("cleanBlockProgressFill");
    if (fill) fill.classList.add("is-indeterminate");
    window.fetch("/api/cleanblock.cgi?action=clean", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: ""
    })
      .then(function (r) { return r.json(); })
      .then(function (res) {
        if (res && res.ok) {
          cleanBlockStartPolling();
        } else {
          cleanBlockFailStart();
        }
      })
      .catch(function () {
        cleanBlockFailStart();
      });
  }

  function cleanBlockFailStart() {
    updateCleanBlockPhaseLabel("error");
    var doneActions = document.getElementById("cleanBlockDoneActions");
    if (doneActions) doneActions.hidden = false;
    var confirmBtn = document.getElementById("cleanBlockConfirmBtn");
    if (confirmBtn) confirmBtn.disabled = false;
  }

  /* Card kebab (three-dot) action menus: toggle on click, close on outside
   * click / Escape. Each .kebab-menu contains a .kebab-btn + .kebab-panel;
   * items are plain buttons that keep their own handlers (e.g. cleanBlockBtn). */
  function closeKebabMenus(exceptMenu) {
    var open = document.querySelectorAll(".kebab-menu.is-open");
    for (var i = 0; i < open.length; i++) {
      var menu = open[i];
      if (exceptMenu && menu === exceptMenu) continue;
      menu.classList.remove("is-open");
      var panel = menu.querySelector(".kebab-panel");
      var btn = menu.querySelector(".kebab-btn");
      if (panel) panel.hidden = true;
      if (btn) btn.setAttribute("aria-expanded", "false");
    }
  }

  function bindKebabMenus() {
    var menus = document.querySelectorAll(".kebab-menu");
    for (var i = 0; i < menus.length; i++) {
      (function (menu) {
        var btn = menu.querySelector(".kebab-btn");
        var panel = menu.querySelector(".kebab-panel");
        if (!btn || !panel) return;
        btn.addEventListener("click", function (event) {
          event.stopPropagation();
          var wasOpen = menu.classList.contains("is-open");
          closeKebabMenus(menu);
          if (!wasOpen) {
            menu.classList.add("is-open");
            panel.hidden = false;
            btn.setAttribute("aria-expanded", "true");
          }
        });
        /* clicking an item closes the menu after its own handler runs */
        panel.addEventListener("click", function (event) {
          event.stopPropagation();
          window.setTimeout(function () { closeKebabMenus(); }, 0);
        });
      })(menus[i]);
    }
    document.addEventListener("click", function () { closeKebabMenus(); });
    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape") closeKebabMenus();
    });
  }

  function bindCleanBlockControls() {
    var btn = document.getElementById("cleanBlockBtn");
    if (btn) {
      btn.addEventListener("click", function () {
        cleanBlockShowView("confirm");
      });
    }
    var cancel = document.getElementById("cleanBlockCancelBtn");
    if (cancel) {
      cancel.addEventListener("click", function () {
        cleanBlockShowView(null);
      });
    }
    var confirm = document.getElementById("cleanBlockConfirmBtn");
    if (confirm) {
      confirm.addEventListener("click", function () {
        cleanBlockTrigger();
      });
    }
    var close = document.getElementById("cleanBlockCloseBtn");
    if (close) {
      close.addEventListener("click", function () {
        cleanBlockActive = false;
        cleanBlockShowView(null);
        var c = document.getElementById("cleanBlockConfirmBtn");
        if (c) c.disabled = false;
      });
    }
  }

  function init() {
    renderDashboard(mock);
    setRefreshCountdown(DASHBOARD_REFRESH_SECONDS);
    bindNav();
    bindLogTabs();
    bindStratumCopy();
    bindWifiControls();
    bindTzControls();
    bindOtaControls();
    bindThemeToggle();
    bindLangSelect();
    applyI18n();
    bindKebabMenus();
    bindCleanBlockControls();
    setTheme(getStoredTheme());
    fetchDashboardData();
    /* prefetch miners cache so the Stratum card pool stats and hero Miners badge fill on first load */
    fetchMinersData();
    startRefreshCountdown();
    startOtaChecking();
    resumeOtaIfApplying();
    /* initial About-page update check after real data has loaded */
    window.setTimeout(aboutCheckUpdate, 3000);

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

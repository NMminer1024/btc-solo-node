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
      storage: [],
      diskTotalUsed: "--",
      diskTotalSize: "--",
      diskTotalPct: null,
      uptime: "--",
      productVersion: "--"
    },
    bitcoin: {
      running: 0,
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
      state: "stopped",
      waitReason: "",
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
      stopped: "Stopped",
      waiting: "Waiting for Bitcoind\u2026"
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

  function storageBarClass(pct) {
    if (pct >= 90) return "is-danger";
    if (pct >= 75) return "is-warn";
    return "";
  }

  function renderStorage(data) {
    var list = document.getElementById("storageList");
    if (!list) return;
    var sys = data.system || {};
    var rows = [];

    // Total disk row (aggregate)
    var totalPct = Number.isFinite(Number(sys.diskTotalPct)) ? Number(sys.diskTotalPct) : 0;
    rows.push({
      label: t("about.totalDisk"),
      value: sys.diskTotalUsed + " / " + sys.diskTotalSize,
      pct: totalPct,
      barId: "storageTotalBar",
      isTotal: true
    });

    // Per-partition rows
    var parts = Array.isArray(sys.storage) ? sys.storage : [];
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i] || {};
      var pct = Number.isFinite(Number(p.pct)) ? Number(p.pct) : 0;
      rows.push({
        label: t("storage." + p.label),
        value: (p.used || "--") + " / " + (p.total || "--"),
        pct: pct,
        barId: "storageBar_" + p.label
      });
    }

    list.innerHTML = "";
    for (var r = 0; r < rows.length; r++) {
      var row = rows[r];
      var wrap = document.createElement("div");
      wrap.className = "storage-row" + (row.isTotal ? " storage-row-total" : "");

      var label = document.createElement("span");
      label.className = "storage-label";
      label.textContent = row.label;

      var bar = document.createElement("div");
      bar.className = "storage-bar";
      var fill = document.createElement("div");
      fill.className = "storage-bar-fill" + (storageBarClass(row.pct) ? " " + storageBarClass(row.pct) : "");
      fill.style.width = Math.max(0, Math.min(100, row.pct)) + "%";
      bar.appendChild(fill);

      var val = document.createElement("strong");
      val.className = "storage-value mono";
      val.textContent = row.value;

      wrap.appendChild(label);
      wrap.appendChild(bar);
      wrap.appendChild(val);
      list.appendChild(wrap);
    }
  }

  function renderSdHealth(sd) {
    var badge = document.getElementById("sdHealthBadge");
    var ids = ["sdHealthCard", "sdHealthWritten", "sdHealthIoErrors", "sdHealthTracking"];
    var eventsEl = document.getElementById("sdHealthEvents");
    if (!sd) {
      if (badge) { badge.textContent = t("sdhealth.unavailable"); badge.className = "status-badge status-soft"; }
      for (var i = 0; i < ids.length; i++) setText(ids[i], "--");
      if (eventsEl) { eventsEl.textContent = ""; eventsEl.hidden = true; }
      return;
    }

    var statusClass = { healthy: "status-ok", degraded: "status-warm", critical: "status-danger", unknown: "status-soft" }[sd.status] || "status-soft";
    var statusKey = "sdhealth.status." + sd.status;
    if (sd.status === "healthy" && sd.verdict === "insufficient_data") {
      statusKey = "sdhealth.status.monitoring";
      statusClass = "status-soft";
    }
    var statusLabel = t(statusKey) !== statusKey ? t(statusKey) : sd.status;
    setBadgeState("sdHealthBadge", statusLabel, statusClass);

    setText("sdHealthCard", sd.cardDate && sd.cardDate !== "--" ? sd.card + " (" + sd.cardDate + ")" : sd.card);
    setText("sdHealthWritten", sd.totalWritten + " \u00b7 " + sd.writeRate);
    setText("sdHealthIoErrors", String(sd.ioErrors) + " \u00b7 " + t("sdhealth.peakWbusy") + " " + (sd.peakWbusy > 0 ? sd.peakWbusy + "%" : "--"));
    setText("sdHealthTracking", t("sdhealth.since") + " " + sd.since + " (" + sd.uptimeDays + " " + t("sdhealth.days") + ")");

    if (eventsEl) {
      var evs = sd.recentEvents || [];
      if (evs.length) {
        eventsEl.textContent = evs.join("\n");
        eventsEl.hidden = false;
      } else {
        eventsEl.textContent = "";
        eventsEl.hidden = true;
      }
    }
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
        storage: Array.isArray(system.storage) ? system.storage : [],
        diskTotalUsed: toText(system.disk_total_used, mock.system.diskTotalUsed),
        diskTotalSize: toText(system.disk_total_size, mock.system.diskTotalSize),
        diskTotalPct: toNumber(system.disk_total_pct, mock.system.diskTotalPct),
        uptime: toText(system.uptime, mock.system.uptime),
        productVersion: toText(system.product_version, mock.system.productVersion),
        sdHealth: normalizeSdHealth(system.sd_health)
      },
      bitcoin: {
        running: toNumber(bitcoin.running, 0),
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
        state: toText(mining.ckpool_state, "stopped"),
        waitReason: toText(mining.ckpool_wait_reason, ""),
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

  function normalizeSdHealth(sh) {
    if (!sh || typeof sh !== "object" || sh.available !== true) return null;
    var card = sh.card || {};
    var cardLabel = [card.vendor, card.name].filter(Boolean).join(" ");
    if (!cardLabel) cardLabel = card.serial || "--";
    return {
      score: toNumber(sh.score, null),
      status: toText(sh.status, "unknown"),
      verdict: toText(sh.verdict, "unknown"),
      card: cardLabel,
      cardDate: toText(card.date, "--"),
      since: toText(sh.baseline_since, "--"),
      totalWritten: toText(sh.total_written_display, "--"),
      writeRate: toText(sh.write_rate_day_display, "--"),
      ioErrors: toNumber(sh.io_errors, 0),
      peakWbusy: toNumber(sh.peak_wbusy_pct, 0),
      uptimeDays: toText(sh.uptime_days_display, String(toNumber(sh.uptime_days, 0))),
      recentEvents: Array.isArray(sh.recent_events) ? sh.recent_events : []
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
      var anchor = li === 0 ? "start" : (li === labelCount - 1 ? "end" : "middle");
      labelParts.push('<text x="' + lx.toFixed(1) + '" y="' + (height - 9) + '" text-anchor="' + anchor + '">' + escapeHtml(formatChartTime(points[lIndex].ts)) + '</text>');
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
    setWifiMessage(data.applyMessage || t("settings.wifiNote"));

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
        setWifiMessage(t("settings.wifiUnavailable"));
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
          setWifiMessage(data.applyMessage || t("settings.wifiUpdated"));
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
      setWifiMessage(t("settings.wifiSsidRequired"));
      return Promise.resolve(null);
    }
    if (ssid.length > 32) {
      setWifiMessage(t("settings.wifiSsidTooLong"));
      return Promise.resolve(null);
    }
    if (pass && (pass.length < 8 || pass.length > 63)) {
      setWifiMessage(t("settings.wifiPassInvalid"));
      return Promise.resolve(null);
    }

    wifiState.applying = true;
    setWifiControlsDisabled(true);
    setWifiMessage(t("settings.wifiApplying"));

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
        setWifiMessage((payload && payload.error) ? payload.error : t("settings.wifiApplyFailed"));
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
      setWifiMessage(t("settings.wifiApplyFailed"));
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

    /* service control buttons — label follows live state */
    var btcSvcBtn = document.getElementById("svcBitcoindToggleBtn");
    if (btcSvcBtn) btcSvcBtn.textContent = t(data.bitcoin.running ? "svc.stopBitcoind" : "svc.startBitcoind");
    var stSvcBtn = document.getElementById("svcStratumToggleBtn");
    if (stSvcBtn) stSvcBtn.textContent = t(data.stratum.state === "running" ? "svc.stopStratum" : "svc.startStratum");

    setText("sysCpu", "Canaan K230 \u00b7 RISC-V");
    setText("sysTemp", data.system.temperature);
    var tempNum = parseFloat(data.system.temperature);
    var tempValid = Number.isFinite(tempNum);
    setText("tempPct", tempValid ? Math.round(tempNum) + "°" : "--");
    setProgress("tempOrb", tempValid ? Math.max(0, Math.min(100, tempNum)) : 0);
    setText("sysTotalStorage", data.system.diskTotalUsed + " / " + data.system.diskTotalSize);
    renderStorage(data);
    renderSdHealth(data.system.sdHealth);

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
        setTzMessage(t("settings.tzUnavailable"));
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
      setTzMessage(t("settings.tzChoose"));
      return Promise.resolve(null);
    }
    tzState.applying = true;
    setTzControlsDisabled(true);
    setTzMessage(t("settings.tzApplying"));
    return tzRequest("apply", { method: "POST", body: "timezone=" + encodeURIComponent(zone) })
      .then(function (payload) {
        tzState.applying = false;
        setTzControlsDisabled(false);
        if (!payload || payload.ok === false) {
          setTzMessage((payload && payload.error) ? payload.error : t("settings.tzApplyFailed"));
          return null;
        }
        tzState.current = toText(payload.timezone, zone);
        setBadgeState("tzStatusBadge", tzState.current, "status-ok");
        setTzMessage(t("settings.tzUpdated").replace("{tz}", escapeHtml(tzState.current)));
        return payload;
      })
      .catch(function () {
        tzState.applying = false;
        setTzControlsDisabled(false);
        setTzMessage(t("settings.tzApplyFailed"));
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

  function otaRenderNotes(notes) {
    var container = document.getElementById("otaNotes");
    var body = document.getElementById("otaNotesBody");
    if (!container || !body) return;
    if (!notes || typeof notes !== "string" || !notes.trim()) {
      container.hidden = true;
      return;
    }
    // Simple markdown: ### headers, - bullets, plain text
    var html = "";
    var lines = notes.split("\n");
    var inList = false;
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].trim();
      if (!line) {
        if (inList) { html += "</ul>"; inList = false; }
        continue;
      }
      if (/^#{1,6}\s+/.test(line)) {
        if (inList) { html += "</ul>"; inList = false; }
        var level = line.match(/^#+/)[0].length;
        var tag = level <= 3 ? "h5" : "p";
        html += "<" + tag + ">" + escapeHtml(line.replace(/^#+\s+/, "")) + "</" + tag + ">";
      } else if (/^[-*]\s+/.test(line)) {
        if (!inList) { html += "<ul>"; inList = true; }
        html += "<li>" + escapeHtml(line.replace(/^[-*]\s+/, "")) + "</li>";
      } else {
        if (inList) { html += "</ul>"; inList = false; }
        html += "<p>" + escapeHtml(line) + "</p>";
      }
    }
    if (inList) html += "</ul>";
    body.innerHTML = html;
    container.hidden = false;
  }

  function otaShowOffer(latest, released, notes) {
    if (otaApplying) return;
    otaOfferedVersion = latest;
    setText("otaCurrentVer", currentProductVersion);
    setText("otaLatestVer", latest);
    setText("otaReleaseDate", released ? t("ota.releasedPrefix") + released : "");
    otaRenderNotes(notes);
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
        otaShowOffer(latest, manifest.released, manifest.notes);
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
      "dash.difficulty": "Mining Difficulty",
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
      "settings.wifiUnavailable": "Wi-Fi settings are unavailable right now.",
      "settings.wifiUpdated": "Wi-Fi profile updated.",
      "settings.wifiSsidRequired": "SSID is required.",
      "settings.wifiSsidTooLong": "SSID must be 32 characters or fewer.",
      "settings.wifiPassInvalid": "Password must be 8-63 characters (or leave blank for open network).",
      "settings.wifiApplying": "Applying Wi-Fi profile. The device may reconnect for a few seconds.",
      "settings.wifiApplyFailed": "Failed to start Wi-Fi apply.",
      "settings.tzUnavailable": "Timezone settings are unavailable right now.",
      "settings.tzChoose": "Please choose a timezone.",
      "settings.tzApplying": "Applying timezone...",
      "settings.tzApplyFailed": "Failed to apply timezone.",
      "settings.tzUpdated": "Timezone updated to {tz}.",
      /* about */
      "about.subtitle": "Versioning, firmware updates, and support diagnostics.",
      "about.firmwareUpdate": "Firmware",
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
      "sys.totalStorage": "Total Storage",
      "about.storage": "Storage",
      "about.totalDisk": "Total Disk",
      "storage.rootfs": "Root FS",
      "storage.config": "Config",
      "storage.block": "Block Data",
      "about.checking": "Checking\u2026",
      "about.checkingNote": "Checking for updates\u2026",
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
      /* sd health */
      "about.sdHealth": "SD Card Health",
      "sdhealth.card": "Card",
      "sdhealth.written": "Written",
      "sdhealth.ioErrors": "I/O Errors",
      "sdhealth.peakWbusy": "peak busy",
      "sdhealth.tracking": "Tracking",
      "sdhealth.since": "since",
      "sdhealth.days": "days",
      "sdhealth.unavailable": "Unavailable",
      "sdhealth.status.healthy": "Healthy",
      "sdhealth.status.monitoring": "Monitoring",
      "sdhealth.status.degraded": "Degraded",
      "sdhealth.status.critical": "Critical",
      "sdhealth.status.unknown": "Unknown",
      /* peer map */
      "peerMap.title": "Peer Map",
      "peerMap.loading": "Locating peers…",
      "peerMap.status": "{n} of {total} peers located",
      "peerMap.empty": "No peers connected.",
      "peerMap.unknown": "Unknown location",
      "peerMap.self": "This node",
      /* ota */
      "ota.available": "Firmware Update Available",
      "ota.whatsNew": "What's New",
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
      "label.stratum.waiting": "Waiting for Bitcoind…",
      "svc.stopBitcoind": "Stop Bitcoind",
      "svc.startBitcoind": "Start Bitcoind",
      "svc.stopStratum": "Stop Stratum",
      "svc.startStratum": "Start Stratum",
      "svc.stopBitcoind.title": "Stop Bitcoind",
      "svc.stopBitcoind.warn": "Stopping Bitcoind pauses block sync and mining. Do not power off.",
      "svc.stopStratum.title": "Stop Stratum",
      "svc.stopStratum.warn": "Mining will stop until Stratum is started again.",
      "svc.stopConfirm": "Stop",
      "svc.startBlocked.title": "Cannot Start Stratum",
      "svc.startBlocked.note": "Stratum will start automatically once Bitcoind is ready.",
      "svc.startBlocked.fallback": "Bitcoind is not ready yet.",
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
      "dash.difficulty": "挖矿难度",
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
      "settings.wifiUnavailable": "Wi-Fi 设置当前不可用。",
      "settings.wifiUpdated": "Wi-Fi 配置已更新。",
      "settings.wifiSsidRequired": "请输入 SSID。",
      "settings.wifiSsidTooLong": "SSID 不能超过 32 个字符。",
      "settings.wifiPassInvalid": "密码需为 8-63 个字符（开放网络可留空）。",
      "settings.wifiApplying": "正在应用 Wi-Fi 配置，设备可能会短暂重连。",
      "settings.wifiApplyFailed": "启动 Wi-Fi 应用失败。",
      "settings.tzUnavailable": "时区设置当前不可用。",
      "settings.tzChoose": "请先选择时区。",
      "settings.tzApplying": "正在应用时区...",
      "settings.tzApplyFailed": "应用时区失败。",
      "settings.tzUpdated": "时区已更新为 {tz}。",
      /* about */
      "about.subtitle": "版本管理、固件更新与支持诊断。",
      "about.firmwareUpdate": "固件",
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
      "sys.totalStorage": "总存储",
      "about.storage": "存储",
      "about.totalDisk": "磁盘总量",
      "storage.rootfs": "根文件系统",
      "storage.config": "配置",
      "storage.block": "区块数据",
      "about.checking": "检查中…",
      "about.checkingNote": "正在检查更新…",
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
      "about.sdHealth": "SD 卡健康",
      "sdhealth.card": "存储卡",
      "sdhealth.written": "累计写入",
      "sdhealth.ioErrors": "I/O 错误",
      "sdhealth.peakWbusy": "峰值写繁忙",
      "sdhealth.tracking": "监测",
      "sdhealth.since": "自",
      "sdhealth.days": "天",
      "sdhealth.unavailable": "不可用",
      "sdhealth.status.healthy": "健康",
      "sdhealth.status.monitoring": "监测中",
      "sdhealth.status.degraded": "降级",
      "sdhealth.status.critical": "严重",
      "sdhealth.status.unknown": "未知",
      "peerMap.title": "节点地图",
      "peerMap.loading": "正在定位节点…",
      "peerMap.status": "已定位 {n}/{total} 个节点",
      "peerMap.empty": "当前没有已连接的节点。",
      "peerMap.unknown": "位置未知",
      "peerMap.self": "本节点",
      /* ota */
      "ota.available": "有可用固件更新",
      "ota.whatsNew": "更新内容",
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
      "label.stratum.waiting": "等待 Bitcoind…",
      "svc.stopBitcoind": "停止 Bitcoind",
      "svc.startBitcoind": "启动 Bitcoind",
      "svc.stopStratum": "停止 Stratum",
      "svc.startStratum": "启动 Stratum",
      "svc.stopBitcoind.title": "停止 Bitcoind",
      "svc.stopBitcoind.warn": "停止 Bitcoind 会暂停区块同步和挖矿。请勿断电。",
      "svc.stopStratum.title": "停止 Stratum",
      "svc.stopStratum.warn": "挖矿将停止，直到再次启动 Stratum。",
      "svc.stopConfirm": "停止",
      "svc.startBlocked.title": "无法启动 Stratum",
      "svc.startBlocked.note": "Bitcoind 就绪后 Stratum 会自动启动。",
      "svc.startBlocked.fallback": "Bitcoind 尚未就绪。",
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
    },
    es: {
      "nav.dashboard": "Panel",
      "nav.miners": "Mineros",
      "nav.logs": "Registros",
      "nav.settings": "Ajustes",
      "nav.about": "Acerca de",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "Tiempo activo",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Nodo BTC solo en Rockbase",
      "dash.loading": "Cargando datos en vivo del nodo...",
      "dash.peers": "Pares",
      "dash.difficulty": "Dificultad de minería",
      "dash.utxo": "Conjunto UTXO",
      "stratum.endpoint": "Endpoint",
      "stratum.hashrate": "Hashrate del pool 5m",
      "stratum.bestShare": "Mejor share",
      "stratum.workersMiners": "Workers / Mineros",
      "sys.title": "Sistema y red",
      "sys.cpu": "Uso de CPU",
      "sys.blockData": "Datos de bloques",
      "sys.ram": "RAM",
      "sys.swap": "Swap",
      "sys.temp": "Temperatura",
      "sys.soc": "SoC",
      "sys.rootfs": "FS raíz",
      "sys.ipMac": "IP y MAC",
      "sys.wifiRssi": "WiFi y RSSI",
      "miners.summary": "Resumen",
      "miners.miners": "Mineros",
      "miners.workers": "Workers",
      "miners.totalHash": "Hashrate total 5m",
      "miners.bestShareEver": "Mejor share histórico",
      "miners.waiting": "Esperando la caché de ckpool...",
      "miners.hashHistory": "Historial de hashrate",
      "miners.noWorkers": "Sin workers.",
      "miners.noMiners": "Aún no hay mineros. Conecta un minero al endpoint Stratum local.",
      "miner.back": "\u2190 Volver a mineros",
      "miner.hash5m": "Hashrate (5m)",
      "miner.totalShares": "Shares totales",
      "miner.bestEver": "Mejor histórico",
      "miner.blockOdds": "Probabilidad de bloque",
      "miner.1d": "1 día",
      "miner.1w": "1 semana",
      "miner.1m": "1 mes",
      "miner.1y": "1 año",
      "miner.workerHistory": "Historial de workers",
      "miner.noData": "Sin datos",
      "miner.online": "\u25cf En línea",
      "miner.offline": "\u25cf Fuera de línea",
      "miner.authorised": "Autorizado",
      "miner.notAuthorised": "No autorizado",
      "miner.lastShare": "Último share",
      "miner.worker": "worker",
      "miner.workers": "workers",
      "logs.summary": "Últimas 300 líneas · cursor en vivo",
      "logs.loading": "Cargando registros...",
      "logs.polling": "Consultando actualizaciones cada 5 s",
      "logs.empty": "[vacío]",
      "settings.network": "Red",
      "settings.wifi": "Configuración de Wi-Fi",
      "settings.ssid": "SSID",
      "settings.password": "Contraseña",
      "settings.wifiNote": "Introduce el SSID y la contraseña del Wi-Fi y pulsa Aplicar. El dispositivo se reconectará automáticamente.",
      "settings.timezone": "Zona horaria",
      "settings.tzNote": "Selecciona una zona horaria y pulsa Aplicar. Se aplica de inmediato.",
      "settings.wifiUnavailable": "La configuración de Wi-Fi no está disponible ahora mismo.",
      "settings.wifiUpdated": "Perfil de Wi-Fi actualizado.",
      "settings.wifiSsidRequired": "El SSID es obligatorio.",
      "settings.wifiSsidTooLong": "El SSID debe tener 32 caracteres o menos.",
      "settings.wifiPassInvalid": "La contraseña debe tener de 8 a 63 caracteres (o déjala vacía para red abierta).",
      "settings.wifiApplying": "Aplicando el perfil de Wi-Fi. El dispositivo puede reconectarse durante unos segundos.",
      "settings.wifiApplyFailed": "No se pudo iniciar la aplicación del Wi-Fi.",
      "settings.tzUnavailable": "La configuración de zona horaria no está disponible ahora mismo.",
      "settings.tzChoose": "Selecciona una zona horaria.",
      "settings.tzApplying": "Aplicando zona horaria...",
      "settings.tzApplyFailed": "No se pudo aplicar la zona horaria.",
      "settings.tzUpdated": "Zona horaria actualizada a {tz}.",
      "about.subtitle": "Versionado, actualizaciones de firmware y diagnóstico de soporte.",
      "about.firmwareUpdate": "Firmware",
      "about.current": "Actual",
      "about.latest": "Última",
      "about.released": "Publicada",
      "about.checkUpdates": "Buscar actualizaciones",
      "about.updateNow": "Actualizar ahora",
      "about.forceUpdate": "Forzar actualización",
      "about.firmware": "Firmware",
      "about.board": "Placa",
      "about.api": "API",
      "about.system": "Sistema",
      "about.productVersion": "Versión del producto",
      "about.lastUpdated": "Última actualización",
      "sys.totalStorage": "Almacenamiento total",
      "about.storage": "Almacenamiento",
      "about.totalDisk": "Disco total",
      "storage.rootfs": "FS raíz",
      "storage.config": "Config",
      "storage.block": "Datos de bloque",
      "about.checking": "Comprobando…",
      "about.checkingNote": "Comprobando actualizaciones…",
      "about.contacting": "Contactando con el servidor de actualizaciones…",
      "about.unavailable": "No disponible",
      "about.unreachable": "No se pudo conectar con el servidor de actualizaciones. Inténtalo más tarde.",
      "about.unknown": "Desconocida",
      "about.unknownFormat": "Formato de versión no reconocido.",
      "about.updateAvailable": "Actualización disponible",
      "about.updateAvailableNote": "Hay una versión de firmware más nueva disponible. Se recomienda actualizar.",
      "about.upToDate": "Al día",
      "about.upToDateNewer": "Estás usando una versión más nueva que la última publicación.",
      "about.upToDateLatest": "Estás usando la versión de firmware más reciente.",
      "about.error": "Error",
      "about.checkFailed": "No se pudieron comprobar las actualizaciones.",
      "about.sdHealth": "Salud de la tarjeta SD",
      "sdhealth.card": "Tarjeta",
      "sdhealth.written": "Escrito",
      "sdhealth.ioErrors": "Errores de E/S",
      "sdhealth.peakWbusy": "pico ocupado",
      "sdhealth.tracking": "Seguimiento",
      "sdhealth.since": "desde",
      "sdhealth.days": "días",
      "sdhealth.unavailable": "No disponible",
      "sdhealth.status.healthy": "Saludable",
      "sdhealth.status.monitoring": "Monitoreando",
      "sdhealth.status.degraded": "Degradado",
      "sdhealth.status.critical": "Crítico",
      "sdhealth.status.unknown": "Desconocido",
      "peerMap.title": "Mapa de pares",
      "peerMap.loading": "Localizando pares…",
      "peerMap.status": "{n} de {total} pares localizados",
      "peerMap.empty": "No hay pares conectados.",
      "peerMap.unknown": "Ubicación desconocida",
      "peerMap.self": "Este nodo",
      "ota.available": "Actualización de firmware disponible",
      "ota.whatsNew": "Novedades",
      "ota.note": "La actualización se descarga, se verifica y se escribe en la ranura de sistema en espera. La sincronización y el minado se pausan durante la actualización. Al terminar, el nodo se reinicia automáticamente. No apagues el equipo.",
      "ota.remindTomorrow": "Recordármelo mañana",
      "ota.updating": "Actualizando firmware",
      "ota.rebootWait": "Reiniciando automáticamente — espera, no apagues el equipo.",
      "ota.doNotPowerOff": "No apagues el nodo.",
      "ota.rebootAuto": "El nuevo sistema se ha escrito y verificado. Reinicio automático en unos segundos...",
      "ota.rebootMsg": "El nodo se está reiniciando. Esta página se recargará cuando vuelva.",
      "ota.updateComplete": "Actualización completada",
      "ota.reloading": "Recargando...",
      "ota.rebootSent": "Orden de reinicio enviada. El nodo se está reiniciando; esta página se recargará cuando vuelva.",
      "ota.errorDefault": "Consulta /root/config/ota/ota.log en el nodo para más detalles.",
      "ota.releasedPrefix": "Publicada ",
      "ota.forceNote": "Estás a punto de instalar forzosamente el firmware {ver}. Esto sobrescribirá la ranura de sistema actual aunque la versión sea igual o más nueva. La sincronización y el minado se pausarán. No apagues el equipo.",
      "ota.phase.preparing": "Preparando...",
      "ota.phase.downloading": "Descargando firmware",
      "ota.phase.verifying": "Verificando firma y suma de control",
      "ota.phase.flashing": "Escribiendo imagen del sistema",
      "ota.phase.reboot_pending": "Escritura completada",
      "ota.phase.switching": "Cambiando al nuevo sistema, reiniciando",
      "ota.phase.failed": "Error en la actualización",
      "label.sync.syncing": "Sincronizando IBD",
      "label.sync.pre_headers": "Pre-sincronización de cabeceras",
      "label.sync.sync_headers": "Sincronizando cabeceras",
      "label.sync.sync_blocks": "Sincronizando bloques",
      "label.sync.loading_index": "Cargando índice de bloques",
      "label.sync.loading_blocks": "Cargando archivos de bloques",
      "label.sync.verifying_blocks": "Verificando bloques",
      "label.sync.rewinding_blocks": "Rebobinando bloques",
      "label.sync.pruning": "Podando bloques",
      "label.sync.synced": "Sincronizado",
      "label.sync.btc_not_running": "No en ejecución",
      "label.prune.pruned_mode": "Nodo podado",
      "label.prune.full_mode": "Nodo completo",
      "label.stratum.running": "En ejecución",
      "label.stratum.stopped": "Detenido",
      "label.stratum.waiting": "Esperando a Bitcoind…",
      "svc.stopBitcoind": "Detener Bitcoind",
      "svc.startBitcoind": "Iniciar Bitcoind",
      "svc.stopStratum": "Detener Stratum",
      "svc.startStratum": "Iniciar Stratum",
      "svc.stopBitcoind.title": "Detener Bitcoind",
      "svc.stopBitcoind.warn": "Detener Bitcoind pausa la sincronización de bloques y la minería. No apague el equipo.",
      "svc.stopStratum.title": "Detener Stratum",
      "svc.stopStratum.warn": "La minería se detendrá hasta que Stratum se vuelva a iniciar.",
      "svc.stopConfirm": "Detener",
      "svc.startBlocked.title": "No se puede iniciar Stratum",
      "svc.startBlocked.note": "Stratum se iniciará automáticamente cuando Bitcoind esté listo.",
      "svc.startBlocked.fallback": "Bitcoind aún no está listo.",
      "wifi.idle": "Inactivo",
      "theme.dark": "Oscuro",
      "theme.light": "Claro",
      "common.loading": "Cargando...",
      "common.apply": "Aplicar",
      "cleanBlock.btn": "Limpiar datos de bloques",
      "cleanBlock.title": "Limpiar datos de bloques",
      "cleanBlock.warn": "Esto eliminará por completo los datos de bloques ya sincronizados en este dispositivo. Tras la eliminación, el nodo volverá a sincronizar todos los bloques desde cero, lo que se espera que tome más de 1 mes. El minado se pausa durante la operación. No apagues el equipo.",
      "cleanBlock.cancel": "Cancelar",
      "cleanBlock.confirm": "Limpiar ahora",
      "cleanBlock.progressTitle": "Limpiando datos de bloques",
      "cleanBlock.progressNote": "No apagues el nodo.",
      "cleanBlock.close": "Cerrar",
      "cleanBlock.phase.stopping": "Deteniendo bitcoind...",
      "cleanBlock.phase.cleaning": "Borrando datos de bloques...",
      "cleanBlock.phase.restarting": "Reiniciando bitcoind...",
      "cleanBlock.phase.done": "Hecho. Re-sincronizando todos los bloques (se espera más de 1 mes).",
      "cleanBlock.phase.error": "La limpieza terminó con un error. Revisa los registros."
    },
    fr: {
      "nav.dashboard": "Tableau de bord",
      "nav.miners": "Mineurs",
      "nav.logs": "Journaux",
      "nav.settings": "Réglages",
      "nav.about": "À propos",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "Disponibilité",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Nœud BTC solo sur Rockbase",
      "dash.loading": "Chargement des données du nœud...",
      "dash.peers": "Pairs",
      "dash.difficulty": "Difficulté de minage",
      "dash.utxo": "Ensemble UTXO",
      "stratum.endpoint": "Point d'accès",
      "stratum.hashrate": "Hashrate du pool 5m",
      "stratum.bestShare": "Meilleur share",
      "stratum.workersMiners": "Workers / Mineurs",
      "sys.title": "Système et réseau",
      "sys.cpu": "Utilisation CPU",
      "sys.blockData": "Données de blocs",
      "sys.ram": "RAM",
      "sys.swap": "Swap",
      "sys.temp": "Température",
      "sys.soc": "SoC",
      "sys.rootfs": "FS racine",
      "sys.ipMac": "IP et MAC",
      "sys.wifiRssi": "WiFi et RSSI",
      "miners.summary": "Résumé",
      "miners.miners": "Mineurs",
      "miners.workers": "Workers",
      "miners.totalHash": "Hashrate total 5m",
      "miners.bestShareEver": "Meilleur share historique",
      "miners.waiting": "En attente du cache ckpool...",
      "miners.hashHistory": "Historique du hashrate",
      "miners.noWorkers": "Aucun worker.",
      "miners.noMiners": "Aucun mineur pour l'instant. Connectez un mineur au point d'accès Stratum local.",
      "miner.back": "\u2190 Retour aux mineurs",
      "miner.hash5m": "Hashrate (5m)",
      "miner.totalShares": "Shares totaux",
      "miner.bestEver": "Meilleur historique",
      "miner.blockOdds": "Probabilité de bloc",
      "miner.1d": "1 jour",
      "miner.1w": "1 semaine",
      "miner.1m": "1 mois",
      "miner.1y": "1 an",
      "miner.workerHistory": "Historique des workers",
      "miner.noData": "Aucune donnée",
      "miner.online": "\u25cf En ligne",
      "miner.offline": "\u25cf Hors ligne",
      "miner.authorised": "Autorisé",
      "miner.notAuthorised": "Non autorisé",
      "miner.lastShare": "Dernier share",
      "miner.worker": "worker",
      "miner.workers": "workers",
      "logs.summary": "300 dernières lignes · curseur en direct",
      "logs.loading": "Chargement des journaux...",
      "logs.polling": "Mise à jour en direct toutes les 5 s",
      "logs.empty": "[vide]",
      "settings.network": "Réseau",
      "settings.wifi": "Configuration Wi-Fi",
      "settings.ssid": "SSID",
      "settings.password": "Mot de passe",
      "settings.wifiNote": "Saisissez le SSID et le mot de passe Wi-Fi, puis appuyez sur Appliquer. L'appareil se reconnectera automatiquement.",
      "settings.timezone": "Fuseau horaire",
      "settings.tzNote": "Sélectionnez un fuseau horaire et appuyez sur Appliquer. Prise d'effet immédiate.",
      "settings.wifiUnavailable": "La configuration Wi-Fi n'est pas disponible pour le moment.",
      "settings.wifiUpdated": "Profil Wi-Fi mis à jour.",
      "settings.wifiSsidRequired": "Le SSID est obligatoire.",
      "settings.wifiSsidTooLong": "Le SSID doit contenir 32 caractères ou moins.",
      "settings.wifiPassInvalid": "Le mot de passe doit contenir de 8 à 63 caractères (ou rester vide pour un réseau ouvert).",
      "settings.wifiApplying": "Application du profil Wi-Fi. L'appareil peut se reconnecter pendant quelques secondes.",
      "settings.wifiApplyFailed": "Échec du démarrage de l'application du Wi-Fi.",
      "settings.tzUnavailable": "La configuration du fuseau horaire n'est pas disponible pour le moment.",
      "settings.tzChoose": "Veuillez choisir un fuseau horaire.",
      "settings.tzApplying": "Application du fuseau horaire...",
      "settings.tzApplyFailed": "Échec de l'application du fuseau horaire.",
      "settings.tzUpdated": "Fuseau horaire mis à jour : {tz}.",
      "about.subtitle": "Versioning, mises à jour du firmware et diagnostics de support.",
      "about.firmwareUpdate": "Firmware",
      "about.current": "Actuelle",
      "about.latest": "Dernière",
      "about.released": "Publiée",
      "about.checkUpdates": "Vérifier les mises à jour",
      "about.updateNow": "Mettre à jour",
      "about.forceUpdate": "Forcer la mise à jour",
      "about.firmware": "Firmware",
      "about.board": "Carte",
      "about.api": "API",
      "about.system": "Système",
      "about.productVersion": "Version produit",
      "about.lastUpdated": "Dernière mise à jour",
      "sys.totalStorage": "Stockage total",
      "about.storage": "Stockage",
      "about.totalDisk": "Disque total",
      "storage.rootfs": "FS racine",
      "storage.config": "Config",
      "storage.block": "Données de bloc",
      "about.checking": "Vérification…",
      "about.checkingNote": "Vérification des mises à jour…",
      "about.contacting": "Connexion au serveur de mise à jour…",
      "about.unavailable": "Indisponible",
      "about.unreachable": "Impossible de joindre le serveur de mise à jour. Réessayez plus tard.",
      "about.unknown": "Inconnue",
      "about.unknownFormat": "Format de version non reconnu.",
      "about.updateAvailable": "Mise à jour disponible",
      "about.updateAvailableNote": "Une version de firmware plus récente est disponible. Mise à jour recommandée.",
      "about.upToDate": "À jour",
      "about.upToDateNewer": "Vous utilisez une version plus récente que la dernière publication.",
      "about.upToDateLatest": "Vous utilisez la version de firmware la plus récente.",
      "about.error": "Erreur",
      "about.checkFailed": "Échec de la vérification des mises à jour.",
      "about.sdHealth": "Santé de la carte SD",
      "sdhealth.card": "Carte",
      "sdhealth.written": "Écrit",
      "sdhealth.ioErrors": "Erreurs E/S",
      "sdhealth.peakWbusy": "pic occupé",
      "sdhealth.tracking": "Suivi",
      "sdhealth.since": "depuis",
      "sdhealth.days": "jours",
      "sdhealth.unavailable": "Indisponible",
      "sdhealth.status.healthy": "Sain",
      "sdhealth.status.monitoring": "Surveillance",
      "sdhealth.status.degraded": "Dégradé",
      "sdhealth.status.critical": "Critique",
      "sdhealth.status.unknown": "Inconnu",
      "peerMap.title": "Carte des pairs",
      "peerMap.loading": "Localisation des pairs…",
      "peerMap.status": "{n} pairs localisés sur {total}",
      "peerMap.empty": "Aucun pair connecté.",
      "peerMap.unknown": "Emplacement inconnu",
      "peerMap.self": "Ce nœud",
      "ota.available": "Mise à jour du firmware disponible",
      "ota.whatsNew": "Nouveautés",
      "ota.note": "La mise à jour est téléchargée, vérifiée et écrite dans la baie système en attente. La synchronisation et le minage sont en pause pendant la mise à jour. À la fin, le nœud redémarre automatiquement. Ne coupez pas le courant.",
      "ota.remindTomorrow": "Me le rappeler demain",
      "ota.updating": "Mise à jour du firmware",
      "ota.rebootWait": "Redémarrage automatique — veuillez patienter, ne coupez pas le courant.",
      "ota.doNotPowerOff": "Ne coupez pas le courant du nœud.",
      "ota.rebootAuto": "Le nouveau système est écrit et vérifié. Redémarrage automatique dans quelques secondes...",
      "ota.rebootMsg": "Le nœud redémarre. Cette page se rechargera à son retour.",
      "ota.updateComplete": "Mise à jour terminée",
      "ota.reloading": "Rechargement...",
      "ota.rebootSent": "Commande de redémarrage envoyée. Le nœud redémarre ; cette page se rechargera à son retour.",
      "ota.errorDefault": "Voir /root/config/ota/ota.log sur le nœud pour plus de détails.",
      "ota.releasedPrefix": "Publiée le ",
      "ota.forceNote": "Vous êtes sur le point d'installer de force le firmware {ver}. Cela écrasera la baie système actuelle même si la version est identique ou plus récente. La synchronisation et le minage seront en pause. Ne coupez pas le courant.",
      "ota.phase.preparing": "Préparation...",
      "ota.phase.downloading": "Téléchargement du firmware",
      "ota.phase.verifying": "Vérification de la signature et de la somme de contrôle",
      "ota.phase.flashing": "Écriture de l'image système",
      "ota.phase.reboot_pending": "Écriture terminée",
      "ota.phase.switching": "Passage au nouveau système, redémarrage",
      "ota.phase.failed": "Échec de la mise à jour",
      "label.sync.syncing": "Synchronisation IBD",
      "label.sync.pre_headers": "Pré-synchronisation des en-têtes",
      "label.sync.sync_headers": "Synchronisation des en-têtes",
      "label.sync.sync_blocks": "Synchronisation des blocs",
      "label.sync.loading_index": "Chargement de l'index de blocs",
      "label.sync.loading_blocks": "Chargement des fichiers de blocs",
      "label.sync.verifying_blocks": "Vérification des blocs",
      "label.sync.rewinding_blocks": "Rembobinage des blocs",
      "label.sync.pruning": "Élagage des blocs",
      "label.sync.synced": "Synchronisé",
      "label.sync.btc_not_running": "Non en cours",
      "label.prune.pruned_mode": "Nœud élagué",
      "label.prune.full_mode": "Nœud complet",
      "label.stratum.running": "En cours",
      "label.stratum.stopped": "Arrêté",
      "label.stratum.waiting": "En attente de Bitcoind…",
      "svc.stopBitcoind": "Arrêter Bitcoind",
      "svc.startBitcoind": "Démarrer Bitcoind",
      "svc.stopStratum": "Arrêter Stratum",
      "svc.startStratum": "Démarrer Stratum",
      "svc.stopBitcoind.title": "Arrêter Bitcoind",
      "svc.stopBitcoind.warn": "L'arrêt de Bitcoind met en pause la synchronisation des blocs et le minage. Ne coupez pas l'alimentation.",
      "svc.stopStratum.title": "Arrêter Stratum",
      "svc.stopStratum.warn": "Le minage sera arrêté jusqu'à ce que Stratum soit relancé.",
      "svc.stopConfirm": "Arrêter",
      "svc.startBlocked.title": "Impossible de démarrer Stratum",
      "svc.startBlocked.note": "Stratum démarrera automatiquement une fois Bitcoind prêt.",
      "svc.startBlocked.fallback": "Bitcoind n'est pas encore prêt.",
      "wifi.idle": "Inactif",
      "theme.dark": "Sombre",
      "theme.light": "Clair",
      "common.loading": "Chargement...",
      "common.apply": "Appliquer",
      "cleanBlock.btn": "Nettoyer les données de blocs",
      "cleanBlock.title": "Nettoyer les données de blocs",
      "cleanBlock.warn": "Cela supprimera définitivement les données de blocs déjà synchronisées sur cet appareil. Après suppression, le nœud resynchronisera tous les blocs depuis zéro, ce qui devrait prendre plus d'un mois. Le minage est en pause pendant l'opération. Ne coupez pas le courant.",
      "cleanBlock.cancel": "Annuler",
      "cleanBlock.confirm": "Nettoyer maintenant",
      "cleanBlock.progressTitle": "Nettoyage des données de blocs",
      "cleanBlock.progressNote": "Ne coupez pas le courant du nœud.",
      "cleanBlock.close": "Fermer",
      "cleanBlock.phase.stopping": "Arrêt de bitcoind...",
      "cleanBlock.phase.cleaning": "Effacement des données de blocs...",
      "cleanBlock.phase.restarting": "Redémarrage de bitcoind...",
      "cleanBlock.phase.done": "Terminé. Resynchronisation de tous les blocs (plus d'un mois attendu).",
      "cleanBlock.phase.error": "Le nettoyage s'est terminé avec une erreur. Consultez les journaux."
    },
    de: {
      "nav.dashboard": "Übersicht",
      "nav.miners": "Miner",
      "nav.logs": "Protokolle",
      "nav.settings": "Einstellungen",
      "nav.about": "Über",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "Laufzeit",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Solo-BTC-Knoten auf Rockbase",
      "dash.loading": "Live-Knotendaten werden geladen...",
      "dash.peers": "Peers",
      "dash.difficulty": "Mining-Schwierigkeit",
      "dash.utxo": "UTXO-Satz",
      "stratum.endpoint": "Endpunkt",
      "stratum.hashrate": "Pool-Hashrate 5m",
      "stratum.bestShare": "Bester Share",
      "stratum.workersMiners": "Workers / Miner",
      "sys.title": "System & Netzwerk",
      "sys.cpu": "CPU-Auslastung",
      "sys.blockData": "Blockdaten",
      "sys.ram": "RAM",
      "sys.swap": "Swap",
      "sys.temp": "Temperatur",
      "sys.soc": "SoC",
      "sys.rootfs": "Root-FS",
      "sys.ipMac": "IP & MAC",
      "sys.wifiRssi": "WLAN & RSSI",
      "miners.summary": "Zusammenfassung",
      "miners.miners": "Miner",
      "miners.workers": "Workers",
      "miners.totalHash": "Gesamt-Hashrate 5m",
      "miners.bestShareEver": "Bester Share insgesamt",
      "miners.waiting": "Warte auf ckpool-Cache...",
      "miners.hashHistory": "Hashrate-Verlauf",
      "miners.noWorkers": "Keine Workers.",
      "miners.noMiners": "Noch keine Miner gefunden. Verbinde einen Miner mit dem lokalen Stratum-Endpunkt.",
      "miner.back": "\u2190 Zurück zu Miner",
      "miner.hash5m": "Hashrate (5m)",
      "miner.totalShares": "Shares gesamt",
      "miner.bestEver": "Bester bisher",
      "miner.blockOdds": "Block-Chancen",
      "miner.1d": "1 Tag",
      "miner.1w": "1 Woche",
      "miner.1m": "1 Monat",
      "miner.1y": "1 Jahr",
      "miner.workerHistory": "Worker-Verlauf",
      "miner.noData": "Keine Daten",
      "miner.online": "\u25cf Online",
      "miner.offline": "\u25cf Offline",
      "miner.authorised": "Autorisiert",
      "miner.notAuthorised": "Nicht autorisiert",
      "miner.lastShare": "Letzter Share",
      "miner.worker": "Worker",
      "miner.workers": "Workers",
      "logs.summary": "Letzte 300 Zeilen · Live-Cursor",
      "logs.loading": "Protokolle werden geladen...",
      "logs.polling": "Live-Updates alle 5 s abgerufen",
      "logs.empty": "[leer]",
      "settings.network": "Netzwerk",
      "settings.wifi": "WLAN-Einrichtung",
      "settings.ssid": "SSID",
      "settings.password": "Passwort",
      "settings.wifiNote": "Gib deinen WLAN-SSID und das Passwort ein und tippe auf Übernehmen. Das Gerät verbindet sich automatisch neu.",
      "settings.timezone": "Zeitzone",
      "settings.tzNote": "Wähle eine Zeitzone und tippe auf Übernehmen. Wirkt sofort.",
      "settings.wifiUnavailable": "WLAN-Einstellungen sind gerade nicht verfügbar.",
      "settings.wifiUpdated": "WLAN-Profil aktualisiert.",
      "settings.wifiSsidRequired": "SSID ist erforderlich.",
      "settings.wifiSsidTooLong": "SSID darf höchstens 32 Zeichen lang sein.",
      "settings.wifiPassInvalid": "Das Passwort muss 8-63 Zeichen lang sein (oder leer lassen für offenes Netzwerk).",
      "settings.wifiApplying": "WLAN-Profil wird übernommen. Das Gerät verbindet sich möglicherweise für wenige Sekunden neu.",
      "settings.wifiApplyFailed": "WLAN-Anwendung konnte nicht gestartet werden.",
      "settings.tzUnavailable": "Zeitzoneneinstellungen sind gerade nicht verfügbar.",
      "settings.tzChoose": "Bitte wähle eine Zeitzone.",
      "settings.tzApplying": "Zeitzone wird übernommen...",
      "settings.tzApplyFailed": "Zeitzone konnte nicht übernommen werden.",
      "settings.tzUpdated": "Zeitzone auf {tz} aktualisiert.",
      "about.subtitle": "Versionierung, Firmware-Updates und Support-Diagnosen.",
      "about.firmwareUpdate": "Firmware",
      "about.current": "Aktuell",
      "about.latest": "Neueste",
      "about.released": "Veröffentlicht",
      "about.checkUpdates": "Nach Updates suchen",
      "about.updateNow": "Jetzt aktualisieren",
      "about.forceUpdate": "Update erzwingen",
      "about.firmware": "Firmware",
      "about.board": "Board",
      "about.api": "API",
      "about.system": "System",
      "about.productVersion": "Produktversion",
      "about.lastUpdated": "Zuletzt aktualisiert",
      "sys.totalStorage": "Gesamtspeicher",
      "about.storage": "Speicher",
      "about.totalDisk": "Gesamtdatenträger",
      "storage.rootfs": "Root-FS",
      "storage.config": "Konfig",
      "storage.block": "Blockdaten",
      "about.checking": "Prüfe…",
      "about.checkingNote": "Suche nach Updates…",
      "about.contacting": "Verbinde mit Update-Server…",
      "about.unavailable": "Nicht verfügbar",
      "about.unreachable": "Update-Server nicht erreichbar. Versuche es später erneut.",
      "about.unknown": "Unbekannt",
      "about.unknownFormat": "Versionsformat nicht erkannt.",
      "about.updateAvailable": "Update verfügbar",
      "about.updateAvailableNote": "Eine neuere Firmware-Version ist verfügbar. Update empfohlen.",
      "about.upToDate": "Auf dem neuesten Stand",
      "about.upToDateNewer": "Du nutzt eine neuere Version als die letzte Veröffentlichung.",
      "about.upToDateLatest": "Du nutzt die neueste Firmware-Version.",
      "about.error": "Fehler",
      "about.checkFailed": "Updates konnten nicht geprüft werden.",
      "about.sdHealth": "SD-Karten-Gesundheit",
      "sdhealth.card": "Karte",
      "sdhealth.written": "Geschrieben",
      "sdhealth.ioErrors": "E/A-Fehler",
      "sdhealth.peakWbusy": "Spitzenlast",
      "sdhealth.tracking": "Überwachung",
      "sdhealth.since": "seit",
      "sdhealth.days": "Tage",
      "sdhealth.unavailable": "Nicht verfügbar",
      "sdhealth.status.healthy": "Gesund",
      "sdhealth.status.monitoring": "Überwacht",
      "sdhealth.status.degraded": "Beeinträchtigt",
      "sdhealth.status.critical": "Kritisch",
      "sdhealth.status.unknown": "Unbekannt",
      "peerMap.title": "Peer-Karte",
      "peerMap.loading": "Peers werden lokalisiert…",
      "peerMap.status": "{n} von {total} Peers lokalisiert",
      "peerMap.empty": "Keine verbundenen Peers.",
      "peerMap.unknown": "Unbekannter Standort",
      "peerMap.self": "Dieser Knoten",
      "ota.available": "Firmware-Update verfügbar",
      "ota.whatsNew": "Neuigkeiten",
      "ota.note": "Das Update wird heruntergeladen, geprüft und in den Standby-Systemslot geschrieben. Synchronisation und Mining werden während des Updates pausiert. Nach Abschluss startet der Knoten automatisch neu. Nicht ausschalten.",
      "ota.remindTomorrow": "Morgen daran erinnern",
      "ota.updating": "Firmware wird aktualisiert",
      "ota.rebootWait": "Startet automatisch neu — bitte warten, nicht ausschalten.",
      "ota.doNotPowerOff": "Knoten nicht ausschalten.",
      "ota.rebootAuto": "Das neue System ist geschrieben und geprüft. Automatischer Neustart in wenigen Sekunden...",
      "ota.rebootMsg": "Der Knoten startet neu. Diese Seite lädt nach dem Wiederherstellen neu.",
      "ota.updateComplete": "Update abgeschlossen",
      "ota.reloading": "Wird neu geladen...",
      "ota.rebootSent": "Neustartbefehl gesendet. Der Knoten startet neu; diese Seite lädt nach dem Wiederherstellen neu.",
      "ota.errorDefault": "Details siehe /root/config/ota/ota.log auf dem Knoten.",
      "ota.releasedPrefix": "Veröffentlicht am ",
      "ota.forceNote": "Du bist dabei, die Firmware {ver} zu erzwingen. Dies überschreibt den aktuellen Systemslot, auch wenn die Version gleich oder neuer ist. Synchronisation und Mining werden pausiert. Nicht ausschalten.",
      "ota.phase.preparing": "Wird vorbereitet...",
      "ota.phase.downloading": "Firmware wird heruntergeladen",
      "ota.phase.verifying": "Signatur & Prüfsumme werden geprüft",
      "ota.phase.flashing": "Systemimage wird geschrieben",
      "ota.phase.reboot_pending": "Schreiben abgeschlossen",
      "ota.phase.switching": "Wechsle zum neuen System, starte neu",
      "ota.phase.failed": "Update fehlgeschlagen",
      "label.sync.syncing": "IBD-Synchronisation",
      "label.sync.pre_headers": "Blockheader vorab synchronisieren",
      "label.sync.sync_headers": "Blockheader werden synchronisiert",
      "label.sync.sync_blocks": "Blöcke werden synchronisiert",
      "label.sync.loading_index": "Blockindex wird geladen",
      "label.sync.loading_blocks": "Blockdateien werden geladen",
      "label.sync.verifying_blocks": "Blöcke werden geprüft",
      "label.sync.rewinding_blocks": "Blöcke werden zurückgewickelt",
      "label.sync.pruning": "Blöcke werden bereinigt",
      "label.sync.synced": "Synchronisiert",
      "label.sync.btc_not_running": "Läuft nicht",
      "label.prune.pruned_mode": "Bereinigter Knoten",
      "label.prune.full_mode": "Vollknoten",
      "label.stratum.running": "Läuft",
      "label.stratum.stopped": "Gestoppt",
      "label.stratum.waiting": "Warte auf Bitcoind…",
      "svc.stopBitcoind": "Bitcoind stoppen",
      "svc.startBitcoind": "Bitcoind starten",
      "svc.stopStratum": "Stratum stoppen",
      "svc.startStratum": "Stratum starten",
      "svc.stopBitcoind.title": "Bitcoind stoppen",
      "svc.stopBitcoind.warn": "Das Stoppen von Bitcoind pausiert die Block-Synchronisation und das Mining. Bitte nicht ausschalten.",
      "svc.stopStratum.title": "Stratum stoppen",
      "svc.stopStratum.warn": "Das Mining wird angehalten, bis Stratum erneut gestartet wird.",
      "svc.stopConfirm": "Stoppen",
      "svc.startBlocked.title": "Stratum kann nicht gestartet werden",
      "svc.startBlocked.note": "Stratum startet automatisch, sobald Bitcoind bereit ist.",
      "svc.startBlocked.fallback": "Bitcoind ist noch nicht bereit.",
      "wifi.idle": "Leerlauf",
      "theme.dark": "Dunkel",
      "theme.light": "Hell",
      "common.loading": "Wird geladen...",
      "common.apply": "Übernehmen",
      "cleanBlock.btn": "Blockdaten bereinigen",
      "cleanBlock.title": "Blockdaten bereinigen",
      "cleanBlock.warn": "Dies löscht die bereits synchronisierten Blockdaten auf diesem Gerät vollständig. Danach synchronisiert der Knoten alle Blöcke von vorn, was voraussichtlich mehr als 1 Monat dauert. Mining wird währenddessen pausiert. Nicht ausschalten.",
      "cleanBlock.cancel": "Abbrechen",
      "cleanBlock.confirm": "Jetzt bereinigen",
      "cleanBlock.progressTitle": "Blockdaten werden bereinigt",
      "cleanBlock.progressNote": "Knoten nicht ausschalten.",
      "cleanBlock.close": "Schließen",
      "cleanBlock.phase.stopping": "bitcoind wird gestoppt...",
      "cleanBlock.phase.cleaning": "Blockdaten werden gelöscht...",
      "cleanBlock.phase.restarting": "bitcoind wird neu gestartet...",
      "cleanBlock.phase.done": "Fertig. Alle Blöcke werden neu synchronisiert (voraussichtlich mehr als 1 Monat).",
      "cleanBlock.phase.error": "Bereinigung mit Fehler beendet. Prüfe die Protokolle."
    },
    pt: {
      "nav.dashboard": "Painel",
      "nav.miners": "Mineradores",
      "nav.logs": "Registros",
      "nav.settings": "Configurações",
      "nav.about": "Sobre",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "Tempo ativo",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Nó BTC solo no Rockbase",
      "dash.loading": "Carregando dados ao vivo do nó...",
      "dash.peers": "Pares",
      "dash.difficulty": "Dificuldade de mineração",
      "dash.utxo": "Conjunto UTXO",
      "stratum.endpoint": "Endpoint",
      "stratum.hashrate": "Hashrate da pool 5m",
      "stratum.bestShare": "Melhor share",
      "stratum.workersMiners": "Workers / Mineradores",
      "sys.title": "Sistema e rede",
      "sys.cpu": "Uso de CPU",
      "sys.blockData": "Dados de blocos",
      "sys.ram": "RAM",
      "sys.swap": "Swap",
      "sys.temp": "Temperatura",
      "sys.soc": "SoC",
      "sys.rootfs": "FS raiz",
      "sys.ipMac": "IP e MAC",
      "sys.wifiRssi": "Wi-Fi e RSSI",
      "miners.summary": "Resumo",
      "miners.miners": "Mineradores",
      "miners.workers": "Workers",
      "miners.totalHash": "Hashrate total 5m",
      "miners.bestShareEver": "Melhor share de sempre",
      "miners.waiting": "Aguardando o cache do ckpool...",
      "miners.hashHistory": "Histórico de hashrate",
      "miners.noWorkers": "Sem workers.",
      "miners.noMiners": "Ainda não há mineradores. Conecte um minerador ao endpoint Stratum local.",
      "miner.back": "\u2190 Voltar aos mineradores",
      "miner.hash5m": "Hashrate (5m)",
      "miner.totalShares": "Shares totais",
      "miner.bestEver": "Melhor de sempre",
      "miner.blockOdds": "Probabilidade de bloco",
      "miner.1d": "1 dia",
      "miner.1w": "1 semana",
      "miner.1m": "1 mês",
      "miner.1y": "1 ano",
      "miner.workerHistory": "Histórico de workers",
      "miner.noData": "Sem dados",
      "miner.online": "\u25cf Online",
      "miner.offline": "\u25cf Offline",
      "miner.authorised": "Autorizado",
      "miner.notAuthorised": "Não autorizado",
      "miner.lastShare": "Último share",
      "miner.worker": "worker",
      "miner.workers": "workers",
      "logs.summary": "Últimas 300 linhas · cursor ao vivo",
      "logs.loading": "Carregando registros...",
      "logs.polling": "Consultando atualizações a cada 5 s",
      "logs.empty": "[vazio]",
      "settings.network": "Rede",
      "settings.wifi": "Configuração do Wi-Fi",
      "settings.ssid": "SSID",
      "settings.password": "Senha",
      "settings.wifiNote": "Insira o SSID e a senha do Wi-Fi e toque em Aplicar. O dispositivo se reconectará automaticamente.",
      "settings.timezone": "Fuso horário",
      "settings.tzNote": "Selecione um fuso horário e toque em Aplicar. Entra em vigor imediatamente.",
      "settings.wifiUnavailable": "As configurações de Wi-Fi não estão disponíveis no momento.",
      "settings.wifiUpdated": "Perfil de Wi-Fi atualizado.",
      "settings.wifiSsidRequired": "O SSID é obrigatório.",
      "settings.wifiSsidTooLong": "O SSID deve ter no máximo 32 caracteres.",
      "settings.wifiPassInvalid": "A senha deve ter de 8 a 63 caracteres (ou deixe em branco para rede aberta).",
      "settings.wifiApplying": "Aplicando o perfil de Wi-Fi. O dispositivo pode se reconectar por alguns segundos.",
      "settings.wifiApplyFailed": "Falha ao iniciar a aplicação do Wi-Fi.",
      "settings.tzUnavailable": "As configurações de fuso horário não estão disponíveis no momento.",
      "settings.tzChoose": "Selecione um fuso horário.",
      "settings.tzApplying": "Aplicando fuso horário...",
      "settings.tzApplyFailed": "Falha ao aplicar o fuso horário.",
      "settings.tzUpdated": "Fuso horário atualizado para {tz}.",
      "about.subtitle": "Versionamento, atualizações de firmware e diagnósticos de suporte.",
      "about.firmwareUpdate": "Firmware",
      "about.current": "Atual",
      "about.latest": "Mais recente",
      "about.released": "Lançada",
      "about.checkUpdates": "Verificar atualizações",
      "about.updateNow": "Atualizar agora",
      "about.forceUpdate": "Forçar atualização",
      "about.firmware": "Firmware",
      "about.board": "Placa",
      "about.api": "API",
      "about.system": "Sistema",
      "about.productVersion": "Versão do produto",
      "about.lastUpdated": "Última atualização",
      "sys.totalStorage": "Armazenamento total",
      "about.storage": "Armazenamento",
      "about.totalDisk": "Disco total",
      "storage.rootfs": "FS raiz",
      "storage.config": "Config",
      "storage.block": "Dados de bloco",
      "about.checking": "Verificando…",
      "about.checkingNote": "Verificando atualizações…",
      "about.contacting": "Contatando o servidor de atualizações…",
      "about.unavailable": "Indisponível",
      "about.unreachable": "Não foi possível conectar ao servidor de atualizações. Tente novamente mais tarde.",
      "about.unknown": "Desconhecida",
      "about.unknownFormat": "Formato de versão não reconhecido.",
      "about.updateAvailable": "Atualização disponível",
      "about.updateAvailableNote": "Há uma versão de firmware mais nova disponível. Atualização recomendada.",
      "about.upToDate": "Atualizado",
      "about.upToDateNewer": "Você está usando uma versão mais nova que o último lançamento.",
      "about.upToDateLatest": "Você está usando a versão de firmware mais recente.",
      "about.error": "Erro",
      "about.checkFailed": "Falha ao verificar atualizações.",
      "about.sdHealth": "Saúde do cartão SD",
      "sdhealth.card": "Cartão",
      "sdhealth.written": "Gravado",
      "sdhealth.ioErrors": "Erros de E/S",
      "sdhealth.peakWbusy": "pico ocupado",
      "sdhealth.tracking": "Monitoramento",
      "sdhealth.since": "desde",
      "sdhealth.days": "dias",
      "sdhealth.unavailable": "Indisponível",
      "sdhealth.status.healthy": "Saudável",
      "sdhealth.status.monitoring": "Monitorando",
      "sdhealth.status.degraded": "Degradado",
      "sdhealth.status.critical": "Crítico",
      "sdhealth.status.unknown": "Desconhecido",
      "peerMap.title": "Mapa de pares",
      "peerMap.loading": "Localizando pares…",
      "peerMap.status": "{n} de {total} pares localizados",
      "peerMap.empty": "Nenhum par conectado.",
      "peerMap.unknown": "Localização desconhecida",
      "peerMap.self": "Este nó",
      "ota.available": "Atualização de firmware disponível",
      "ota.whatsNew": "Novidades",
      "ota.note": "A atualização é baixada, verificada e gravada no slot de sistema em espera. A sincronização e a mineração ficam em pausa durante a atualização. Ao terminar, o nó reinicia automaticamente. Não desligue.",
      "ota.remindTomorrow": "Lembrar-me amanhã",
      "ota.updating": "Atualizando firmware",
      "ota.rebootWait": "Reiniciando automaticamente — aguarde, não desligue.",
      "ota.doNotPowerOff": "Não desligue o nó.",
      "ota.rebootAuto": "O novo sistema foi gravado e verificado. Reinício automático em alguns segundos...",
      "ota.rebootMsg": "O nó está reiniciando. Esta página será recarregada quando voltar.",
      "ota.updateComplete": "Atualização concluída",
      "ota.reloading": "Recarregando...",
      "ota.rebootSent": "Comando de reinício enviado. O nó está reiniciando; esta página será recarregada quando voltar.",
      "ota.errorDefault": "Veja /root/config/ota/ota.log no nó para detalhes.",
      "ota.releasedPrefix": "Lançada em ",
      "ota.forceNote": "Você está prestes a instalar forçadamente o firmware {ver}. Isso sobrescreverá o slot de sistema atual mesmo que a versão seja igual ou mais nova. A sincronização e a mineração ficarão em pausa. Não desligue.",
      "ota.phase.preparing": "Preparando...",
      "ota.phase.downloading": "Baixando firmware",
      "ota.phase.verifying": "Verificando assinatura e soma de verificação",
      "ota.phase.flashing": "Gravando imagem do sistema",
      "ota.phase.reboot_pending": "Gravação concluída",
      "ota.phase.switching": "Mudando para o novo sistema, reiniciando",
      "ota.phase.failed": "Falha na atualização",
      "label.sync.syncing": "Sincronizando IBD",
      "label.sync.pre_headers": "Pré-sincronização de cabeçalhos",
      "label.sync.sync_headers": "Sincronizando cabeçalhos",
      "label.sync.sync_blocks": "Sincronizando blocos",
      "label.sync.loading_index": "Carregando índice de blocos",
      "label.sync.loading_blocks": "Carregando arquivos de blocos",
      "label.sync.verifying_blocks": "Verificando blocos",
      "label.sync.rewinding_blocks": "Rebobinando blocos",
      "label.sync.pruning": "Podando blocos",
      "label.sync.synced": "Sincronizado",
      "label.sync.btc_not_running": "Não em execução",
      "label.prune.pruned_mode": "Nó podado",
      "label.prune.full_mode": "Nó completo",
      "label.stratum.running": "Em execução",
      "label.stratum.stopped": "Parado",
      "label.stratum.waiting": "Aguardando o Bitcoind…",
      "svc.stopBitcoind": "Parar Bitcoind",
      "svc.startBitcoind": "Iniciar Bitcoind",
      "svc.stopStratum": "Parar Stratum",
      "svc.startStratum": "Iniciar Stratum",
      "svc.stopBitcoind.title": "Parar Bitcoind",
      "svc.stopBitcoind.warn": "Parar o Bitcoind pausa a sincronização de blocos e a mineração. Não desligue o aparelho.",
      "svc.stopStratum.title": "Parar Stratum",
      "svc.stopStratum.warn": "A mineração será interrompida até que o Stratum seja iniciado novamente.",
      "svc.stopConfirm": "Parar",
      "svc.startBlocked.title": "Não é possível iniciar o Stratum",
      "svc.startBlocked.note": "O Stratum será iniciado automaticamente quando o Bitcoind estiver pronto.",
      "svc.startBlocked.fallback": "O Bitcoind ainda não está pronto.",
      "wifi.idle": "Ocioso",
      "theme.dark": "Escuro",
      "theme.light": "Claro",
      "common.loading": "Carregando...",
      "common.apply": "Aplicar",
      "cleanBlock.btn": "Limpar dados de blocos",
      "cleanBlock.title": "Limpar dados de blocos",
      "cleanBlock.warn": "Isso excluirá completamente os dados de blocos já sincronizados neste dispositivo. Após a exclusão, o nó ressincronizará todos os blocos do zero, o que deve levar mais de 1 mês. A mineração fica em pausa durante a operação. Não desligue.",
      "cleanBlock.cancel": "Cancelar",
      "cleanBlock.confirm": "Limpar agora",
      "cleanBlock.progressTitle": "Limpar dados de blocos",
      "cleanBlock.progressNote": "Não desligue o nó.",
      "cleanBlock.close": "Fechar",
      "cleanBlock.phase.stopping": "Parando o bitcoind...",
      "cleanBlock.phase.cleaning": "Apagando dados de blocos...",
      "cleanBlock.phase.restarting": "Reiniciando o bitcoind...",
      "cleanBlock.phase.done": "Concluído. Ressincronizando todos os blocos (esperado mais de 1 mês).",
      "cleanBlock.phase.error": "A limpeza terminou com erro. Verifique os registros."
    },
    ru: {
      "nav.dashboard": "Панель",
      "nav.miners": "Майнеры",
      "nav.logs": "Журналы",
      "nav.settings": "Настройки",
      "nav.about": "О системе",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "Аптайм",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Соло BTC-узел на Rockbase",
      "dash.loading": "Загрузка данных узла...",
      "dash.peers": "Соседи",
      "dash.difficulty": "Сложность майнинга",
      "dash.utxo": "Набор UTXO",
      "stratum.endpoint": "Эндпоинт",
      "stratum.hashrate": "Хешрейт пула 5м",
      "stratum.bestShare": "Лучший шар",
      "stratum.workersMiners": "Воркеры / Майнеры",
      "sys.title": "Система и сеть",
      "sys.cpu": "Загрузка CPU",
      "sys.blockData": "Данные блоков",
      "sys.ram": "ОЗУ",
      "sys.swap": "Подкачка",
      "sys.temp": "Температура",
      "sys.soc": "SoC",
      "sys.rootfs": "Корневая ФС",
      "sys.ipMac": "IP и MAC",
      "sys.wifiRssi": "Wi-Fi и RSSI",
      "miners.summary": "Сводка",
      "miners.miners": "Майнеры",
      "miners.workers": "Воркеры",
      "miners.totalHash": "Общий хешрейт 5м",
      "miners.bestShareEver": "Лучший шар за всё время",
      "miners.waiting": "Ожидание кэша ckpool...",
      "miners.hashHistory": "История хешрейта",
      "miners.noWorkers": "Нет воркеров.",
      "miners.noMiners": "Майнеры пока не найдены. Подключите майнер к локальному Stratum-эндпоинту.",
      "miner.back": "\u2190 Назад к майнерам",
      "miner.hash5m": "Хешрейт (5м)",
      "miner.totalShares": "Всего шаров",
      "miner.bestEver": "Лучший за всё время",
      "miner.blockOdds": "Шансы на блок",
      "miner.1d": "1 день",
      "miner.1w": "1 неделя",
      "miner.1m": "1 месяц",
      "miner.1y": "1 год",
      "miner.workerHistory": "История воркеров",
      "miner.noData": "Нет данных",
      "miner.online": "\u25cf В сети",
      "miner.offline": "\u25cf Не в сети",
      "miner.authorised": "Авторизован",
      "miner.notAuthorised": "Не авторизован",
      "miner.lastShare": "Последний шар",
      "miner.worker": "воркер",
      "miner.workers": "воркеров",
      "logs.summary": "Последние 300 строк · живой курсор",
      "logs.loading": "Загрузка журналов...",
      "logs.polling": "Опрос обновлений каждые 5 с",
      "logs.empty": "[пусто]",
      "settings.network": "Сеть",
      "settings.wifi": "Настройка Wi-Fi",
      "settings.ssid": "SSID",
      "settings.password": "Пароль",
      "settings.wifiNote": "Введите SSID и пароль Wi-Fi, затем нажмите «Применить». Устройство переподключится автоматически.",
      "settings.timezone": "Часовой пояс",
      "settings.tzNote": "Выберите часовой пояс и нажмите «Применить». Вступает в силу сразу.",
      "settings.wifiUnavailable": "Настройки Wi-Fi сейчас недоступны.",
      "settings.wifiUpdated": "Профиль Wi-Fi обновлён.",
      "settings.wifiSsidRequired": "Укажите SSID.",
      "settings.wifiSsidTooLong": "SSID должен содержать не более 32 символов.",
      "settings.wifiPassInvalid": "Пароль должен содержать от 8 до 63 символов (или оставьте пустым для открытой сети).",
      "settings.wifiApplying": "Применение профиля Wi-Fi. Устройство может переподключиться на несколько секунд.",
      "settings.wifiApplyFailed": "Не удалось запустить применение Wi-Fi.",
      "settings.tzUnavailable": "Настройки часового пояса сейчас недоступны.",
      "settings.tzChoose": "Выберите часовой пояс.",
      "settings.tzApplying": "Применение часового пояса...",
      "settings.tzApplyFailed": "Не удалось применить часовой пояс.",
      "settings.tzUpdated": "Часовой пояс изменён на {tz}.",
      "about.subtitle": "Версионирование, обновления прошивки и диагностика поддержки.",
      "about.firmwareUpdate": "Прошивка",
      "about.current": "Текущая",
      "about.latest": "Последняя",
      "about.released": "Выпущена",
      "about.checkUpdates": "Проверить обновления",
      "about.updateNow": "Обновить сейчас",
      "about.forceUpdate": "Принудительное обновление",
      "about.firmware": "Прошивка",
      "about.board": "Плата",
      "about.api": "API",
      "about.system": "Система",
      "about.productVersion": "Версия продукта",
      "about.lastUpdated": "Последнее обновление",
      "sys.totalStorage": "Всего хранилища",
      "about.storage": "Хранилище",
      "about.totalDisk": "Всего диска",
      "storage.rootfs": "Корневая ФС",
      "storage.config": "Конфиг",
      "storage.block": "Данные блоков",
      "about.checking": "Проверка…",
      "about.checkingNote": "Проверка обновлений…",
      "about.contacting": "Соединение с сервером обновлений…",
      "about.unavailable": "Недоступно",
      "about.unreachable": "Не удалось подключиться к серверу обновлений. Попробуйте позже.",
      "about.unknown": "Неизвестно",
      "about.unknownFormat": "Формат версии не распознан.",
      "about.updateAvailable": "Доступно обновление",
      "about.updateAvailableNote": "Доступна более новая версия прошивки. Рекомендуется обновить.",
      "about.upToDate": "Актуально",
      "about.upToDateNewer": "Вы используете версию новее последнего выпуска.",
      "about.upToDateLatest": "Вы используете последнюю версию прошивки.",
      "about.error": "Ошибка",
      "about.checkFailed": "Не удалось проверить обновления.",
      "about.sdHealth": "Состояние SD-карты",
      "sdhealth.card": "Карта",
      "sdhealth.written": "Записано",
      "sdhealth.ioErrors": "Ошибки ввода-вывода",
      "sdhealth.peakWbusy": "пик загрузки",
      "sdhealth.tracking": "Наблюдение",
      "sdhealth.since": "с",
      "sdhealth.days": "дн.",
      "sdhealth.unavailable": "Недоступно",
      "sdhealth.status.healthy": "Исправно",
      "sdhealth.status.monitoring": "Наблюдение",
      "sdhealth.status.degraded": "Ухудшено",
      "sdhealth.status.critical": "Критично",
      "sdhealth.status.unknown": "Неизвестно",
      "peerMap.title": "Карта узлов",
      "peerMap.loading": "Определение местоположения узлов…",
      "peerMap.status": "Определено местоположение {n} из {total} узлов",
      "peerMap.empty": "Нет подключённых узлов.",
      "peerMap.unknown": "Неизвестное местоположение",
      "peerMap.self": "Этот узел",
      "ota.available": "Доступно обновление прошивки",
      "ota.whatsNew": "Что нового",
      "ota.note": "Обновление скачивается, проверяется и записывается в резервный системный слот. Синхронизация и майнинг приостанавливаются во время обновления. После записи узел автоматически перезагрузится. Не отключайте питание.",
      "ota.remindTomorrow": "Напомнить завтра",
      "ota.updating": "Обновление прошивки",
      "ota.rebootWait": "Автоматическая перезагрузка — подождите, не отключайте питание.",
      "ota.doNotPowerOff": "Не отключайте питание узла.",
      "ota.rebootAuto": "Новая система записана и проверена. Автоматическая перезагрузка через несколько секунд...",
      "ota.rebootMsg": "Узел перезагружается. Страница обновится после его возвращения.",
      "ota.updateComplete": "Обновление завершено",
      "ota.reloading": "Перезагрузка...",
      "ota.rebootSent": "Команда перезагрузки отправлена. Узел перезагружается; страница обновится после его возвращения.",
      "ota.errorDefault": "См. /root/config/ota/ota.log на узле для деталей.",
      "ota.releasedPrefix": "Выпущена ",
      "ota.forceNote": "Вы собираетесь принудительно установить прошивку {ver}. Это перезапишет текущий системный слот, даже если версия та же или новее. Синхронизация и майнинг будут приостановлены. Не отключайте питание.",
      "ota.phase.preparing": "Подготовка...",
      "ota.phase.downloading": "Загрузка прошивки",
      "ota.phase.verifying": "Проверка подписи и контрольной суммы",
      "ota.phase.flashing": "Запись системного образа",
      "ota.phase.reboot_pending": "Запись завершена",
      "ota.phase.switching": "Переключение на новую систему, перезагрузка",
      "ota.phase.failed": "Ошибка обновления",
      "label.sync.syncing": "Синхронизация IBD",
      "label.sync.pre_headers": "Предварительная синхронизация заголовков",
      "label.sync.sync_headers": "Синхронизация заголовков",
      "label.sync.sync_blocks": "Синхронизация блоков",
      "label.sync.loading_index": "Загрузка индекса блоков",
      "label.sync.loading_blocks": "Загрузка файлов блоков",
      "label.sync.verifying_blocks": "Проверка блоков",
      "label.sync.rewinding_blocks": "Откат блоков",
      "label.sync.pruning": "Обрезка блоков",
      "label.sync.synced": "Синхронизировано",
      "label.sync.btc_not_running": "Не запущен",
      "label.prune.pruned_mode": "Обрезанный узел",
      "label.prune.full_mode": "Полный узел",
      "label.stratum.running": "Работает",
      "label.stratum.stopped": "Остановлен",
      "label.stratum.waiting": "Ожидание Bitcoind…",
      "svc.stopBitcoind": "Остановить Bitcoind",
      "svc.startBitcoind": "Запустить Bitcoind",
      "svc.stopStratum": "Остановить Stratum",
      "svc.startStratum": "Запустить Stratum",
      "svc.stopBitcoind.title": "Остановить Bitcoind",
      "svc.stopBitcoind.warn": "Остановка Bitcoind приостанавливает синхронизацию блоков и майнинг. Не выключайте питание.",
      "svc.stopStratum.title": "Остановить Stratum",
      "svc.stopStratum.warn": "Майнинг будет остановлен, пока Stratum не будет запущен снова.",
      "svc.stopConfirm": "Остановить",
      "svc.startBlocked.title": "Невозможно запустить Stratum",
      "svc.startBlocked.note": "Stratum запустится автоматически, как только Bitcoind будет готов.",
      "svc.startBlocked.fallback": "Bitcoind ещё не готов.",
      "wifi.idle": "Ожидание",
      "theme.dark": "Тёмная",
      "theme.light": "Светлая",
      "common.loading": "Загрузка...",
      "common.apply": "Применить",
      "cleanBlock.btn": "Очистить данные блоков",
      "cleanBlock.title": "Очистить данные блоков",
      "cleanBlock.warn": "Это полностью удалит уже синхронизированные данные блоков на этом устройстве. После удаления узел заново синхронизирует все блоки с нуля, что займёт более 1 месяца. Майнинг приостанавливается во время операции. Не отключайте питание.",
      "cleanBlock.cancel": "Отмена",
      "cleanBlock.confirm": "Очистить сейчас",
      "cleanBlock.progressTitle": "Очистка данных блоков",
      "cleanBlock.progressNote": "Не отключайте питание узла.",
      "cleanBlock.close": "Закрыть",
      "cleanBlock.phase.stopping": "Остановка bitcoind...",
      "cleanBlock.phase.cleaning": "Стирание данных блоков...",
      "cleanBlock.phase.restarting": "Перезапуск bitcoind...",
      "cleanBlock.phase.done": "Готово. Повторная синхронизация всех блоков (ожидается более 1 месяца).",
      "cleanBlock.phase.error": "Очистка завершилась с ошибкой. Проверьте журналы."
    },
    ja: {
      "nav.dashboard": "ダッシュボード",
      "nav.miners": "マイナー",
      "nav.logs": "ログ",
      "nav.settings": "設定",
      "nav.about": "情報",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "稼働時間",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Rockbase ソロ BTC ノード",
      "dash.loading": "ノードのライブデータを読み込み中...",
      "dash.peers": "ピア",
      "dash.difficulty": "マイニング難易度",
      "dash.utxo": "UTXO セット",
      "stratum.endpoint": "エンドポイント",
      "stratum.hashrate": "プールハッシュレート 5m",
      "stratum.bestShare": "ベストシェア",
      "stratum.workersMiners": "ワーカー / マイナー",
      "sys.title": "システムとネットワーク",
      "sys.cpu": "CPU 使用率",
      "sys.blockData": "ブロックデータ",
      "sys.ram": "RAM",
      "sys.swap": "スワップ",
      "sys.temp": "温度",
      "sys.soc": "SoC",
      "sys.rootfs": "ルート FS",
      "sys.ipMac": "IP と MAC",
      "sys.wifiRssi": "WiFi と RSSI",
      "miners.summary": "概要",
      "miners.miners": "マイナー",
      "miners.workers": "ワーカー",
      "miners.totalHash": "総ハッシュレート 5m",
      "miners.bestShareEver": "過去最高シェア",
      "miners.waiting": "ckpool キャッシュを待機中...",
      "miners.hashHistory": "ハッシュレート履歴",
      "miners.noWorkers": "ワーカーがありません。",
      "miners.noMiners": "まだマイナーが見つかりません。ローカル Stratum エンドポイントにマイナーを接続してください。",
      "miner.back": "\u2190 マイナーに戻る",
      "miner.hash5m": "ハッシュレート (5m)",
      "miner.totalShares": "総シェア数",
      "miner.bestEver": "過去最高",
      "miner.blockOdds": "ブロック獲得確率",
      "miner.1d": "1 日",
      "miner.1w": "1 週間",
      "miner.1m": "1 か月",
      "miner.1y": "1 年",
      "miner.workerHistory": "ワーカー履歴",
      "miner.noData": "データなし",
      "miner.online": "\u25cf オンライン",
      "miner.offline": "\u25cf オフライン",
      "miner.authorised": "承認済み",
      "miner.notAuthorised": "未承認",
      "miner.lastShare": "直近シェア",
      "miner.worker": "ワーカー",
      "miner.workers": "ワーカー",
      "logs.summary": "直近 300 行 · ライブカーソル",
      "logs.loading": "ログを読み込み中...",
      "logs.polling": "5 秒ごとにライブ更新を取得",
      "logs.empty": "[空]",
      "settings.network": "ネットワーク",
      "settings.wifi": "Wi-Fi 設定",
      "settings.ssid": "SSID",
      "settings.password": "パスワード",
      "settings.wifiNote": "Wi-Fi の SSID とパスワードを入力し、「適用」をタップしてください。デバイスは自動的に再接続します。",
      "settings.timezone": "タイムゾーン",
      "settings.tzNote": "タイムゾーンを選択して「適用」をタップしてください。すぐに反映されます。",
      "settings.wifiUnavailable": "Wi-Fi 設定は現在利用できません。",
      "settings.wifiUpdated": "Wi-Fi プロファイルを更新しました。",
      "settings.wifiSsidRequired": "SSID を入力してください。",
      "settings.wifiSsidTooLong": "SSID は 32 文字以内で入力してください。",
      "settings.wifiPassInvalid": "パスワードは 8〜63 文字で入力してください（オープンネットワークの場合は空欄可）。",
      "settings.wifiApplying": "Wi-Fi プロファイルを適用中です。デバイスは数秒間再接続する場合があります。",
      "settings.wifiApplyFailed": "Wi-Fi 適用の開始に失敗しました。",
      "settings.tzUnavailable": "タイムゾーン設定は現在利用できません。",
      "settings.tzChoose": "タイムゾーンを選択してください。",
      "settings.tzApplying": "タイムゾーンを適用中...",
      "settings.tzApplyFailed": "タイムゾーンの適用に失敗しました。",
      "settings.tzUpdated": "タイムゾーンを {tz} に更新しました。",
      "about.subtitle": "バージョン管理、ファームウェア更新、サポート診断。",
      "about.firmwareUpdate": "ファームウェア",
      "about.current": "現在",
      "about.latest": "最新",
      "about.released": "リリース日",
      "about.checkUpdates": "更新を確認",
      "about.updateNow": "今すぐ更新",
      "about.forceUpdate": "強制更新",
      "about.firmware": "ファームウェア",
      "about.board": "ボード",
      "about.api": "API",
      "about.system": "システム",
      "about.productVersion": "製品バージョン",
      "about.lastUpdated": "最終更新",
      "sys.totalStorage": "総ストレージ",
      "about.storage": "ストレージ",
      "about.totalDisk": "ディスク合計",
      "storage.rootfs": "ルート FS",
      "storage.config": "設定",
      "storage.block": "ブロックデータ",
      "about.checking": "確認中…",
      "about.checkingNote": "アップデートを確認中…",
      "about.contacting": "更新サーバーに接続中…",
      "about.unavailable": "利用不可",
      "about.unreachable": "更新サーバーに接続できません。後でもう一度お試しください。",
      "about.unknown": "不明",
      "about.unknownFormat": "バージョン形式を認識できません。",
      "about.updateAvailable": "更新あり",
      "about.updateAvailableNote": "新しいファームウェアバージョンが利用可能です。更新をおすすめします。",
      "about.upToDate": "最新です",
      "about.upToDateNewer": "最新のリリースより新しいバージョンを使用しています。",
      "about.upToDateLatest": "最新のファームウェアバージョンを使用しています。",
      "about.error": "エラー",
      "about.checkFailed": "更新の確認に失敗しました。",
      "about.sdHealth": "SDカードの状態",
      "sdhealth.card": "カード",
      "sdhealth.written": "書き込み量",
      "sdhealth.ioErrors": "I/Oエラー",
      "sdhealth.peakWbusy": "ピーク負荷",
      "sdhealth.tracking": "監視",
      "sdhealth.since": "開始",
      "sdhealth.days": "日",
      "sdhealth.unavailable": "利用不可",
      "sdhealth.status.healthy": "正常",
      "sdhealth.status.monitoring": "監視中",
      "sdhealth.status.degraded": "劣化",
      "sdhealth.status.critical": "重大",
      "sdhealth.status.unknown": "不明",
      "peerMap.title": "ピアマップ",
      "peerMap.loading": "ピアの位置を特定中…",
      "peerMap.status": "ピア {total} 件中 {n} 件の位置を特定",
      "peerMap.empty": "接続中のピアがありません。",
      "peerMap.unknown": "位置不明",
      "peerMap.self": "このノード",
      "ota.available": "ファームウェア更新があります",
      "ota.whatsNew": "新機能",
      "ota.note": "更新はダウンロードされ、検証されて、スタンバイのシステムスロットに書き込まれます。アップグレード中は同期とマイニングが一時停止されます。書き込みが完了するとノードは自動的に再起動します。電源を切らないでください。",
      "ota.remindTomorrow": "明日リマインド",
      "ota.updating": "ファームウェアを更新中",
      "ota.rebootWait": "自動再起動中 — お待ちください。電源を切らないでください。",
      "ota.doNotPowerOff": "ノードの電源を切らないでください。",
      "ota.rebootAuto": "新しいシステムが書き込まれ検証されました。数秒後に自動再起動します...",
      "ota.rebootMsg": "ノードが再起動中です。復帰後にこのページが再読み込みされます。",
      "ota.updateComplete": "更新完了",
      "ota.reloading": "再読み込み中...",
      "ota.rebootSent": "再起動コマンドを送信しました。ノードが再起動中です。復帰後にこのページが再読み込みされます。",
      "ota.errorDefault": "詳細はノード上の /root/config/ota/ota.log を参照してください。",
      "ota.releasedPrefix": "リリース日 ",
      "ota.forceNote": "ファームウェア {ver} を強制インストールしようとしています。バージョンが同じまたは新しい場合でも、現在のシステムスロットを上書きします。アップグレード中は同期とマイニングが一時停止されます。電源を切らないでください。",
      "ota.phase.preparing": "準備中...",
      "ota.phase.downloading": "ファームウェアをダウンロード中",
      "ota.phase.verifying": "署名とチェックサムを検証中",
      "ota.phase.flashing": "システムイメージを書き込み中",
      "ota.phase.reboot_pending": "書き込み完了",
      "ota.phase.switching": "新しいシステムに切り替え、再起動中",
      "ota.phase.failed": "更新に失敗しました",
      "label.sync.syncing": "IBD 同期中",
      "label.sync.pre_headers": "ブロックヘッダー事前同期",
      "label.sync.sync_headers": "ブロックヘッダー同期中",
      "label.sync.sync_blocks": "ブロック同期中",
      "label.sync.loading_index": "ブロックインデックス読み込み中",
      "label.sync.loading_blocks": "ブロックファイル読み込み中",
      "label.sync.verifying_blocks": "ブロック検証中",
      "label.sync.rewinding_blocks": "ブロック巻き戻し中",
      "label.sync.pruning": "ブロック剪定中",
      "label.sync.synced": "同期済み",
      "label.sync.btc_not_running": "未実行",
      "label.prune.pruned_mode": "プルーニングノード",
      "label.prune.full_mode": "フルノード",
      "label.stratum.running": "実行中",
      "label.stratum.stopped": "停止中",
      "label.stratum.waiting": "Bitcoind を待機中…",
      "svc.stopBitcoind": "Bitcoind を停止",
      "svc.startBitcoind": "Bitcoind を起動",
      "svc.stopStratum": "Stratum を停止",
      "svc.startStratum": "Stratum を起動",
      "svc.stopBitcoind.title": "Bitcoind を停止",
      "svc.stopBitcoind.warn": "Bitcoind を停止するとブロック同期とマイニングが一時停止します。電源を切らないでください。",
      "svc.stopStratum.title": "Stratum を停止",
      "svc.stopStratum.warn": "Stratum が再度起動されるまでマイニングは停止します。",
      "svc.stopConfirm": "停止",
      "svc.startBlocked.title": "Stratum を起動できません",
      "svc.startBlocked.note": "Bitcoind が準備できると Stratum は自動的に起動します。",
      "svc.startBlocked.fallback": "Bitcoind はまだ準備できていません。",
      "wifi.idle": "アイドル",
      "theme.dark": "ダーク",
      "theme.light": "ライト",
      "common.loading": "読み込み中...",
      "common.apply": "適用",
      "cleanBlock.btn": "ブロックデータを削除",
      "cleanBlock.title": "ブロックデータを削除",
      "cleanBlock.warn": "これにより、このデバイスに同期済みのブロックデータが完全に削除されます。削除後、ノードはすべてのブロックをゼロから再同期する必要があり、1 か月以上かかる見込みです。処理中はマイニングが一時停止されます。電源を切らないでください。",
      "cleanBlock.cancel": "キャンセル",
      "cleanBlock.confirm": "今すぐ削除",
      "cleanBlock.progressTitle": "ブロックデータを削除中",
      "cleanBlock.progressNote": "ノードの電源を切らないでください。",
      "cleanBlock.close": "閉じる",
      "cleanBlock.phase.stopping": "bitcoind を停止中...",
      "cleanBlock.phase.cleaning": "ブロックデータを消去中...",
      "cleanBlock.phase.restarting": "bitcoind を再起動中...",
      "cleanBlock.phase.done": "完了。すべてのブロックを再同期中（1 か月以上かかる見込み）。",
      "cleanBlock.phase.error": "削除がエラーで終了しました。ログを確認してください。"
    },
    ko: {
      "nav.dashboard": "대시보드",
      "nav.miners": "마이너",
      "nav.logs": "로그",
      "nav.settings": "설정",
      "nav.about": "정보",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "가동 시간",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 Rockbase 솔로 BTC 노드",
      "dash.loading": "노드 실시간 데이터를 불러오는 중...",
      "dash.peers": "피어",
      "dash.difficulty": "채굴 난이도",
      "dash.utxo": "UTXO 세트",
      "stratum.endpoint": "엔드포인트",
      "stratum.hashrate": "풀 해시레이트 5m",
      "stratum.bestShare": "최고 셰어",
      "stratum.workersMiners": "워커 / 마이너",
      "sys.title": "시스템 및 네트워크",
      "sys.cpu": "CPU 사용률",
      "sys.blockData": "블록 데이터",
      "sys.ram": "RAM",
      "sys.swap": "스왑",
      "sys.temp": "온도",
      "sys.soc": "SoC",
      "sys.rootfs": "루트 FS",
      "sys.ipMac": "IP 및 MAC",
      "sys.wifiRssi": "WiFi 및 RSSI",
      "miners.summary": "요약",
      "miners.miners": "마이너",
      "miners.workers": "워커",
      "miners.totalHash": "총 해시레이트 5m",
      "miners.bestShareEver": "역대 최고 셰어",
      "miners.waiting": "ckpool 캐시를 기다리는 중...",
      "miners.hashHistory": "해시레이트 기록",
      "miners.noWorkers": "워커가 없습니다.",
      "miners.noMiners": "아직 마이너가 없습니다. 로컬 Stratum 엔드포인트에 마이너를 연결하세요.",
      "miner.back": "\u2190 마이너로 돌아가기",
      "miner.hash5m": "해시레이트 (5m)",
      "miner.totalShares": "총 셰어",
      "miner.bestEver": "역대 최고",
      "miner.blockOdds": "블록 획득 확률",
      "miner.1d": "1일",
      "miner.1w": "1주",
      "miner.1m": "1개월",
      "miner.1y": "1년",
      "miner.workerHistory": "워커 기록",
      "miner.noData": "데이터 없음",
      "miner.online": "\u25cf 온라인",
      "miner.offline": "\u25cf 오프라인",
      "miner.authorised": "승인됨",
      "miner.notAuthorised": "미승인",
      "miner.lastShare": "최근 셰어",
      "miner.worker": "워커",
      "miner.workers": "워커",
      "logs.summary": "최근 300줄 · 라이브 커서",
      "logs.loading": "로그를 불러오는 중...",
      "logs.polling": "5초마다 라이브 업데이트 조회",
      "logs.empty": "[비어 있음]",
      "settings.network": "네트워크",
      "settings.wifi": "Wi-Fi 설정",
      "settings.ssid": "SSID",
      "settings.password": "비밀번호",
      "settings.wifiNote": "Wi-Fi SSID와 비밀번호를 입력한 뒤 적용을 누르세요. 장치가 자동으로 다시 연결됩니다.",
      "settings.timezone": "시간대",
      "settings.tzNote": "시간대를 선택하고 적용을 누르세요. 즉시 적용됩니다.",
      "settings.wifiUnavailable": "Wi-Fi 설정을 현재 사용할 수 없습니다.",
      "settings.wifiUpdated": "Wi-Fi 프로필이 업데이트되었습니다.",
      "settings.wifiSsidRequired": "SSID를 입력하세요.",
      "settings.wifiSsidTooLong": "SSID는 32자 이하여야 합니다.",
      "settings.wifiPassInvalid": "비밀번호는 8~63자여야 합니다(오픈 네트워크의 경우 비워 둘 수 있음).",
      "settings.wifiApplying": "Wi-Fi 프로필을 적용하는 중입니다. 장치가 몇 초간 다시 연결될 수 있습니다.",
      "settings.wifiApplyFailed": "Wi-Fi 적용을 시작하지 못했습니다.",
      "settings.tzUnavailable": "시간대 설정을 현재 사용할 수 없습니다.",
      "settings.tzChoose": "시간대를 선택하세요.",
      "settings.tzApplying": "시간대를 적용하는 중...",
      "settings.tzApplyFailed": "시간대 적용에 실패했습니다.",
      "settings.tzUpdated": "시간대가 {tz}(으)로 업데이트되었습니다.",
      "about.subtitle": "버전 관리, 펌웨어 업데이트, 지원 진단.",
      "about.firmwareUpdate": "펌웨어",
      "about.current": "현재",
      "about.latest": "최신",
      "about.released": "발행일",
      "about.checkUpdates": "업데이트 확인",
      "about.updateNow": "지금 업데이트",
      "about.forceUpdate": "강제 업데이트",
      "about.firmware": "펌웨어",
      "about.board": "보드",
      "about.api": "API",
      "about.system": "시스템",
      "about.productVersion": "제품 버전",
      "about.lastUpdated": "마지막 업데이트",
      "sys.totalStorage": "총 저장 공간",
      "about.storage": "저장 공간",
      "about.totalDisk": "디스크 전체",
      "storage.rootfs": "루트 FS",
      "storage.config": "설정",
      "storage.block": "블록 데이터",
      "about.checking": "확인 중…",
      "about.checkingNote": "업데이트 확인 중…",
      "about.contacting": "업데이트 서버에 연결하는 중…",
      "about.unavailable": "사용 불가",
      "about.unreachable": "업데이트 서버에 연결할 수 없습니다. 나중에 다시 시도하세요.",
      "about.unknown": "알 수 없음",
      "about.unknownFormat": "버전 형식을 인식할 수 없습니다.",
      "about.updateAvailable": "업데이트 가능",
      "about.updateAvailableNote": "더 새로운 펌웨어 버전이 있습니다. 업데이트를 권장합니다.",
      "about.upToDate": "최신입니다",
      "about.upToDateNewer": "최신 릴리스보다 새로운 버전을 사용 중입니다.",
      "about.upToDateLatest": "최신 펌웨어 버전을 사용 중입니다.",
      "about.error": "오류",
      "about.checkFailed": "업데이트 확인에 실패했습니다.",
      "about.sdHealth": "SD 카드 상태",
      "sdhealth.card": "카드",
      "sdhealth.written": "쓰기량",
      "sdhealth.ioErrors": "I/O 오류",
      "sdhealth.peakWbusy": "피크 부하",
      "sdhealth.tracking": "모니터링",
      "sdhealth.since": "시작",
      "sdhealth.days": "일",
      "sdhealth.unavailable": "사용 불가",
      "sdhealth.status.healthy": "정상",
      "sdhealth.status.monitoring": "모니터링 중",
      "sdhealth.status.degraded": "저하됨",
      "sdhealth.status.critical": "심각",
      "sdhealth.status.unknown": "알 수 없음",
      "peerMap.title": "피어 맵",
      "peerMap.loading": "피어 위치를 확인하는 중…",
      "peerMap.status": "피어 {total}개 중 {n}개 위치 확인",
      "peerMap.empty": "연결된 피어가 없습니다.",
      "peerMap.unknown": "위치 미상",
      "peerMap.self": "이 노드",
      "ota.available": "펌웨어 업데이트가 있습니다",
      "ota.whatsNew": "새로운 내용",
      "ota.note": "업데이트는 다운로드되고 검증된 뒤 대기 시스템 슬롯에 기록됩니다. 업그레이드 동안 동기화 및 채굴이 일시 중지됩니다. 기록이 끝나면 노드가 자동으로 재부팅됩니다. 전원을 끄지 마세요.",
      "ota.remindTomorrow": "내일 알림",
      "ota.updating": "펌웨어 업데이트 중",
      "ota.rebootWait": "자동 재부팅 중 — 잠시 기다려 주세요. 전원을 끄지 마세요.",
      "ota.doNotPowerOff": "노드 전원을 끄지 마세요.",
      "ota.rebootAuto": "새 시스템이 기록되고 검증되었습니다. 몇 초 후 자동으로 재부팅됩니다...",
      "ota.rebootMsg": "노드가 재부팅 중입니다. 복귀 후 이 페이지가 새로고침됩니다.",
      "ota.updateComplete": "업데이트 완료",
      "ota.reloading": "새로고침 중...",
      "ota.rebootSent": "재부팅 명령을 보냈습니다. 노드가 재부팅 중이며 복귀 후 이 페이지가 새로고침됩니다.",
      "ota.errorDefault": "자세한 내용은 노드의 /root/config/ota/ota.log를 확인하세요.",
      "ota.releasedPrefix": "발행일 ",
      "ota.forceNote": "펌웨어 {ver}를 강제 설치하려고 합니다. 버전이 같거나 더 새 버전이어도 현재 시스템 슬롯을 덮어씁니다. 업그레이드 동안 동기화 및 채굴이 일시 중지됩니다. 전원을 끄지 마세요.",
      "ota.phase.preparing": "준비 중...",
      "ota.phase.downloading": "펌웨어 다운로드 중",
      "ota.phase.verifying": "서명 및 체크섬 검증 중",
      "ota.phase.flashing": "시스템 이미지 기록 중",
      "ota.phase.reboot_pending": "기록 완료",
      "ota.phase.switching": "새 시스템으로 전환, 재부팅 중",
      "ota.phase.failed": "업데이트 실패",
      "label.sync.syncing": "IBD 동기화 중",
      "label.sync.pre_headers": "블록 헤더 사전 동기화",
      "label.sync.sync_headers": "블록 헤더 동기화 중",
      "label.sync.sync_blocks": "블록 동기화 중",
      "label.sync.loading_index": "블록 인덱스 불러오는 중",
      "label.sync.loading_blocks": "블록 파일 불러오는 중",
      "label.sync.verifying_blocks": "블록 검증 중",
      "label.sync.rewinding_blocks": "블록 되감기 중",
      "label.sync.pruning": "블록 가지치기 중",
      "label.sync.synced": "동기화됨",
      "label.sync.btc_not_running": "실행 안 됨",
      "label.prune.pruned_mode": "프루닝 노드",
      "label.prune.full_mode": "풀 노드",
      "label.stratum.running": "실행 중",
      "label.stratum.stopped": "정지됨",
      "label.stratum.waiting": "Bitcoind 대기 중…",
      "svc.stopBitcoind": "Bitcoind 중지",
      "svc.startBitcoind": "Bitcoind 시작",
      "svc.stopStratum": "Stratum 중지",
      "svc.startStratum": "Stratum 시작",
      "svc.stopBitcoind.title": "Bitcoind 중지",
      "svc.stopBitcoind.warn": "Bitcoind를 중지하면 블록 동기화와 채굴이 일시 중단됩니다. 전원을 끄지 마세요.",
      "svc.stopStratum.title": "Stratum 중지",
      "svc.stopStratum.warn": "Stratum이 다시 시작될 때까지 채굴이 중단됩니다.",
      "svc.stopConfirm": "중지",
      "svc.startBlocked.title": "Stratum을 시작할 수 없습니다",
      "svc.startBlocked.note": "Bitcoind가 준비되면 Stratum이 자동으로 시작됩니다.",
      "svc.startBlocked.fallback": "Bitcoind가 아직 준비되지 않았습니다.",
      "wifi.idle": "유휴",
      "theme.dark": "다크",
      "theme.light": "라이트",
      "common.loading": "불러오는 중...",
      "common.apply": "적용",
      "cleanBlock.btn": "블록 데이터 삭제",
      "cleanBlock.title": "블록 데이터 삭제",
      "cleanBlock.warn": "이 작업은 이 장치에 이미 동기화된 블록 데이터를 완전히 삭제합니다. 삭제 후 노드는 모든 블록을 처음부터 다시 동기화해야 하며, 1개월 이상 걸릴 것으로 예상됩니다. 작업 동안 채굴이 일시 중지됩니다. 전원을 끄지 마세요.",
      "cleanBlock.cancel": "취소",
      "cleanBlock.confirm": "지금 삭제",
      "cleanBlock.progressTitle": "블록 데이터 삭제 중",
      "cleanBlock.progressNote": "노드 전원을 끄지 마세요.",
      "cleanBlock.close": "닫기",
      "cleanBlock.phase.stopping": "bitcoind 중지 중...",
      "cleanBlock.phase.cleaning": "블록 데이터 지우는 중...",
      "cleanBlock.phase.restarting": "bitcoind 재시작 중...",
      "cleanBlock.phase.done": "완료. 모든 블록을 다시 동기화하는 중 (1개월 이상 소요 예상).",
      "cleanBlock.phase.error": "삭제가 오류로 종료되었습니다. 로그를 확인하세요."
    },
    ar: {
      "nav.dashboard": "لوحة التحكم",
      "nav.miners": "الناطرون",
      "nav.logs": "السجلات",
      "nav.settings": "الإعدادات",
      "nav.about": "حول",
      "nav.bitcoind": "Bitcoind",
      "nav.stratum": "Stratum",
      "topbar.uptime": "زمن التشغيل",
      "footer.copyright": "\u00a9 2026 NMMiner \u00b7 عقدة BTC فردية على Rockbase",
      "dash.loading": "جارٍ تحميل بيانات العقدة المباشرة...",
      "dash.peers": "الأقران",
      "dash.difficulty": "صعوبة التعدين",
      "dash.utxo": "مجموعة UTXO",
      "stratum.endpoint": "نقطة النهاية",
      "stratum.hashrate": "معدل تجزئة المسبح 5د",
      "stratum.bestShare": "أفضل سهم",
      "stratum.workersMiners": "العاملون / الناظرون",
      "sys.title": "النظام والشبكة",
      "sys.cpu": "استخدام المعالج",
      "sys.blockData": "بيانات الكتل",
      "sys.ram": "الذاكرة",
      "sys.swap": "التبديل",
      "sys.temp": "الحرارة",
      "sys.soc": "SoC",
      "sys.rootfs": "نظام الملفات الجذري",
      "sys.ipMac": "IP و MAC",
      "sys.wifiRssi": "Wi-Fi و RSSI",
      "miners.summary": "ملخص",
      "miners.miners": "الناطرون",
      "miners.workers": "العاملون",
      "miners.totalHash": "إجمالي معدل التجزئة 5د",
      "miners.bestShareEver": "أفضل سهم على الإطلاق",
      "miners.waiting": "بانتظار ذاكرة ckpool المؤقتة...",
      "miners.hashHistory": "سجل معدل التجزئة",
      "miners.noWorkers": "لا يوجد عاملون.",
      "miners.noMiners": "لم يتم العثور على ناظرين بعد. اربط ناظراً بنقطة نهاية Stratum المحلية.",
      "miner.back": "\u2190 العودة إلى الناظرين",
      "miner.hash5m": "معدل التجزئة (5د)",
      "miner.totalShares": "إجمالي الأسهم",
      "miner.bestEver": "الأفضل على الإطلاق",
      "miner.blockOdds": "فرص الكتلة",
      "miner.1d": "يوم واحد",
      "miner.1w": "أسبوع واحد",
      "miner.1m": "شهر واحد",
      "miner.1y": "سنة واحدة",
      "miner.workerHistory": "سجل العاملين",
      "miner.noData": "لا توجد بيانات",
      "miner.online": "\u25cf متصل",
      "miner.offline": "\u25cf غير متصل",
      "miner.authorised": "مصرح به",
      "miner.notAuthorised": "غير مصرح به",
      "miner.lastShare": "آخر سهم",
      "miner.worker": "عامل",
      "miner.workers": "عاملون",
      "logs.summary": "آخر 300 سطر · مؤشر مباشر",
      "logs.loading": "جارٍ تحميل السجلات...",
      "logs.polling": "استعلام التحديثات المباشرة كل 5 ثوانٍ",
      "logs.empty": "[فارغ]",
      "settings.network": "الشبكة",
      "settings.wifi": "إعداد Wi-Fi",
      "settings.ssid": "SSID",
      "settings.password": "كلمة المرور",
      "settings.wifiNote": "أدخل اسم شبكة Wi-Fi وكلمة المرور ثم اضغط تطبيق. سيعيد الجهاز الاتصال تلقائيًا.",
      "settings.timezone": "المنطقة الزمنية",
      "settings.tzNote": "اختر منطقة زمنية واضغط تطبيق. يسري المفعول فورًا.",
      "settings.wifiUnavailable": "إعدادات Wi-Fi غير متاحة حاليًا.",
      "settings.wifiUpdated": "تم تحديث ملف Wi-Fi.",
      "settings.wifiSsidRequired": "اسم الشبكة (SSID) مطلوب.",
      "settings.wifiSsidTooLong": "يجب ألا يتجاوز اسم الشبكة 32 حرفًا.",
      "settings.wifiPassInvalid": "يجب أن تتكون كلمة المرور من 8 إلى 63 حرفًا (أو اتركها فارغة لشبكة مفتوحة).",
      "settings.wifiApplying": "جارٍ تطبيق ملف Wi-Fi. قد يعيد الجهاز الاتصال لبضع ثوانٍ.",
      "settings.wifiApplyFailed": "فشل بدء تطبيق Wi-Fi.",
      "settings.tzUnavailable": "إعدادات المنطقة الزمنية غير متاحة حاليًا.",
      "settings.tzChoose": "يرجى اختيار منطقة زمنية.",
      "settings.tzApplying": "جارٍ تطبيق المنطقة الزمنية...",
      "settings.tzApplyFailed": "فشل تطبيق المنطقة الزمنية.",
      "settings.tzUpdated": "تم تحديث المنطقة الزمنية إلى {tz}.",
      "about.subtitle": "إدارة الإصدارات، تحديثات البرامج الثابتة، وتشخيصات الدعم.",
      "about.firmwareUpdate": "البرنامج الثابت",
      "about.current": "الحالي",
      "about.latest": "الأحدث",
      "about.released": "تاريخ الإصدار",
      "about.checkUpdates": "التحقق من التحديثات",
      "about.updateNow": "تحديث الآن",
      "about.forceUpdate": "تحديث إجباري",
      "about.firmware": "البرنامج الثابت",
      "about.board": "اللوحة",
      "about.api": "API",
      "about.system": "النظام",
      "about.productVersion": "إصدار المنتج",
      "about.lastUpdated": "آخر تحديث",
      "sys.totalStorage": "التخزين الإجمالي",
      "about.storage": "التخزين",
      "about.totalDisk": "إجمالي القرص",
      "storage.rootfs": "نظام الملفات الجذري",
      "storage.config": "الإعدادات",
      "storage.block": "بيانات الكتلة",
      "about.checking": "جارٍ التحقق…",
      "about.checkingNote": "جارٍ التحقق من التحديثات…",
      "about.contacting": "جارٍ الاتصال بخادم التحديثات…",
      "about.unavailable": "غير متاح",
      "about.unreachable": "تعذر الوصول إلى خادم التحديثات. حاول مرة أخرى لاحقًا.",
      "about.unknown": "غير معروف",
      "about.unknownFormat": "تنسيق الإصدار غير معترف به.",
      "about.updateAvailable": "تحديث متاح",
      "about.updateAvailableNote": "يتوفر إصدار أحدث من البرنامج الثابت. يُنصح بالتحديث.",
      "about.upToDate": "محدّث",
      "about.upToDateNewer": "تستخدم إصدارًا أحدث من أحدث إصدار.",
      "about.upToDateLatest": "تستخدم أحدث إصدار من البرنامج الثابت.",
      "about.error": "خطأ",
      "about.checkFailed": "فشل التحقق من التحديثات.",
      "about.sdHealth": "صحة بطاقة SD",
      "sdhealth.card": "البطاقة",
      "sdhealth.written": "المكتوب",
      "sdhealth.ioErrors": "أخطاء الإدخال/الإخراج",
      "sdhealth.peakWbusy": "ذروة الانشغال",
      "sdhealth.tracking": "التتبع",
      "sdhealth.since": "منذ",
      "sdhealth.days": "يوم",
      "sdhealth.unavailable": "غير متاح",
      "sdhealth.status.healthy": "سليم",
      "sdhealth.status.monitoring": "قيد الرصد",
      "sdhealth.status.degraded": "متدهور",
      "sdhealth.status.critical": "حرج",
      "sdhealth.status.unknown": "غير معروف",
      "peerMap.title": "خريطة الأقران",
      "peerMap.loading": "جارٍ تحديد مواقع الأقران…",
      "peerMap.status": "تم تحديد موقع {n} من أصل {total} أقران",
      "peerMap.empty": "لا يوجد أقران متصلون.",
      "peerMap.unknown": "موقع غير معروف",
      "peerMap.self": "هذا العقد",
      "ota.available": "تحديث البرنامج الثابت متاح",
      "ota.whatsNew": "ما الجديد",
      "ota.note": "يتم تنزيل التحديث وتحققه وكتابته في فتحة النظام الاحتياطية. يتوقف المزامنة والتعدين أثناء الترقية. عند انتهاء الكتابة تعاد تشغيل العقدة تلقائيًا. لا تقطع الطاقة.",
      "ota.remindTomorrow": "ذكّرني غدًا",
      "ota.updating": "جارٍ تحديث البرنامج الثابت",
      "ota.rebootWait": "جارٍ إعادة التشغيل تلقائيًا — يرجى الانتظار، لا تقطع الطاقة.",
      "ota.doNotPowerOff": "لا تقطع طاقة العقدة.",
      "ota.rebootAuto": "تمت كتابة النظام الجديد وتحققه. إعادة تشغيل تلقائية خلال ثوانٍ...",
      "ota.rebootMsg": "العقدة قيد إعادة التشغيل. ستُعاد تحميل هذه الصفحة عند عودتها.",
      "ota.updateComplete": "اكتمل التحديث",
      "ota.reloading": "جارٍ إعادة التحميل...",
      "ota.rebootSent": "تم إرسال أمر إعادة التشغيل. العقدة قيد إعادة التشغيل وستُعاد تحميل هذه الصفحة عند عودتها.",
      "ota.errorDefault": "راجع /root/config/ota/ota.log على العقدة للتفاصيل.",
      "ota.releasedPrefix": "صدر في ",
      "ota.forceNote": "أنت على وشك تثبيت البرنامج الثابت {ver} إجباريًا. سيؤدي هذا إلى تجاوز فتحة النظام الحالية حتى لو كان الإصدار هو نفسه أو أحدث. سيتوقف المزامنة والتعدين أثناء الترقية. لا تقطع الطاقة.",
      "ota.phase.preparing": "جارٍ التحضير...",
      "ota.phase.downloading": "جارٍ تنزيل البرنامج الثابت",
      "ota.phase.verifying": "جارٍ التحقق من التوقيع ومجموع الفحص",
      "ota.phase.flashing": "جارٍ كتابة صورة النظام",
      "ota.phase.reboot_pending": "اكتمل الكتابة",
      "ota.phase.switching": "التبديل إلى النظام الجديد، إعادة تشغيل",
      "ota.phase.failed": "فشل التحديث",
      "label.sync.syncing": "مزامنة IBD",
      "label.sync.pre_headers": "مزامنة مسبقة لرؤوس الكتل",
      "label.sync.sync_headers": "مزامنة رؤوس الكتل",
      "label.sync.sync_blocks": "مزامنة الكتل",
      "label.sync.loading_index": "تحميل فهرس الكتل",
      "label.sync.loading_blocks": "تحميل ملفات الكتل",
      "label.sync.verifying_blocks": "التحقق من الكتل",
      "label.sync.rewinding_blocks": "إرجاع الكتل",
      "label.sync.pruning": "تقليم الكتل",
      "label.sync.synced": "تمت المزامنة",
      "label.sync.btc_not_running": "غير قيد التشغيل",
      "label.prune.pruned_mode": "عقدة مقصوصة",
      "label.prune.full_mode": "عقدة كاملة",
      "label.stratum.running": "قيد التشغيل",
      "label.stratum.stopped": "متوقف",
      "label.stratum.waiting": "بانتظار Bitcoind…",
      "svc.stopBitcoind": "إيقاف Bitcoind",
      "svc.startBitcoind": "تشغيل Bitcoind",
      "svc.stopStratum": "إيقاف Stratum",
      "svc.startStratum": "تشغيل Stratum",
      "svc.stopBitcoind.title": "إيقاف Bitcoind",
      "svc.stopBitcoind.warn": "إيقاف Bitcoind يوقف مزامنة الكتل والتعدين مؤقتًا. لا تقطع الطاقة.",
      "svc.stopStratum.title": "إيقاف Stratum",
      "svc.stopStratum.warn": "سيتم إيقاف التعدين حتى يتم تشغيل Stratum مرة أخرى.",
      "svc.stopConfirm": "إيقاف",
      "svc.startBlocked.title": "تعذر تشغيل Stratum",
      "svc.startBlocked.note": "سيتم تشغيل Stratum تلقائيًا بمجرد جاهزية Bitcoind.",
      "svc.startBlocked.fallback": "Bitcoind غير جاهز بعد.",
      "wifi.idle": "خامل",
      "theme.dark": "داكن",
      "theme.light": "فاتح",
      "common.loading": "جارٍ التحميل...",
      "common.apply": "تطبيق",
      "cleanBlock.btn": "مسح بيانات الكتل",
      "cleanBlock.title": "مسح بيانات الكتل",
      "cleanBlock.warn": "سيؤدي هذا إلى حذف بيانات الكتل المتزامنة مسبقًا على هذا الجهاز بالكامل. بعد الحذف ستعيد العقدة مزامنة جميع الكتل من الصفر، ومن المتوقع أن يستغرق الأمر أكثر من شهر. يتوقف التعدين أثناء العملية. لا تقطع الطاقة.",
      "cleanBlock.cancel": "إلغاء",
      "cleanBlock.confirm": "مسح الآن",
      "cleanBlock.progressTitle": "جارٍ مسح بيانات الكتل",
      "cleanBlock.progressNote": "لا تقطع طاقة العقدة.",
      "cleanBlock.close": "إغلاق",
      "cleanBlock.phase.stopping": "جارٍ إيقاف bitcoind...",
      "cleanBlock.phase.cleaning": "جارٍ مسح بيانات الكتل...",
      "cleanBlock.phase.restarting": "جارٍ إعادة تشغيل bitcoind...",
      "cleanBlock.phase.done": "تم. جارٍ إعادة مزامنة جميع الكتل (من المتوقع أكثر من شهر).",
      "cleanBlock.phase.error": "انتهى المسح بخطأ. راجع السجلات."
    }
  };

  /* Supported language codes. All 10 languages in #langSelect have full
   * translations (en/zh/es/fr/de/pt/ru/ja/ko/ar). The t() helper still falls
   * back to English for any missing key, so a partial dictionary degrades
   * gracefully. Adding a language = adding an I18N[<code>] dictionary. */
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

  /* ── Peer map ───────────────────────────────────────────
   * Click the Peers count on the Bitcoind card -> overlay with a dot-grid
   * world map. The board only reports peer IPs (status API "peers");
   * geolocation is resolved on the board via /api/geoip.cgi (offline
   * GeoLite2-City.mmdb lookup, no external network needed). */
  var peerMapGeoCache = {}; // ip -> {lat, lon, country, city} | {unknown: true}

  function peerMapGetIps() {
    var api = lastDashboardApi;
    var peers = api && api.bitcoin && Array.isArray(api.bitcoin.peers) ? api.bitcoin.peers : [];
    return peers.filter(function (ip) {
      return typeof ip === "string" && /^\d{1,3}(\.\d{1,3}){3}$/.test(ip);
    });
  }

  var peerMapMarkers = []; // {x, y, ip, city, country} in canvas coords
  var peerMapLandPath = null; // cached Path2D (land + borders)
  var peerMapGratPath = null; // cached Path2D (graticule)
  var peerMapPoints = []; // last resolved points (for animated redraws)
  var peerMapInbound = {}; // ip -> true if the connection is inbound
  var peerMapAnimId = null; // requestAnimationFrame handle

  function peerMapProject(lat, lon, w, h) {
    // equirectangular using the map's actual lat range (85..-60) and the
    // map's visual center longitude (105E, China centered). The world is
    // wrapped so the seam sits at lonCenter-180.
    var wm = window.WORLD_MAP;
    var latMax = wm && wm.latMax != null ? wm.latMax : 85;
    var latMin = wm && wm.latMin != null ? wm.latMin : -60;
    var lonCenter = wm && wm.lonCenter != null ? wm.lonCenter : 0;
    var lo = lonCenter - 180;
    var nlon = ((lon - lo) % 360 + 360) % 360 + lo;
    var x = ((nlon - lo) / 360) * w;
    var y = ((latMax - lat) / (latMax - latMin)) * h;
    return { x: x, y: y };
  }

  /* One animation frame: static map + flowing arcs + markers + self star.
   * t is elapsed seconds, used to scroll the dash pattern along the arcs. */
  function peerMapDrawFrame(points, t) {
    var canvas = document.getElementById("peerMapCanvas");
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#0b0e14";
    ctx.fillRect(0, 0, w, h);

    // vector landmass from embedded WORLD_MAP (Natural Earth 110m country
    // boundaries as SVG paths, pre-projected to the canvas size)
    var wm = window.WORLD_MAP;
    if (wm && wm.land) {
      if (!peerMapLandPath) peerMapLandPath = new Path2D(wm.land.join(""));
      if (!peerMapGratPath) peerMapGratPath = new Path2D(wm.graticule);
      ctx.strokeStyle = "rgba(120, 130, 150, 0.35)";
      ctx.lineWidth = 1;
      ctx.stroke(peerMapGratPath);
      ctx.fillStyle = "rgba(130, 140, 160, 0.55)";
      ctx.fill(peerMapLandPath);
      ctx.strokeStyle = "rgba(170, 180, 200, 0.8)";
      ctx.lineWidth = 1;
      ctx.stroke(peerMapLandPath);
    }

    // this node's own location (computed first: flow arcs originate here)
    var api = lastDashboardApi;
    var selfGeo = api && api.bitcoin && api.bitcoin.self_geo;
    var selfPos = null;
    if (selfGeo && selfGeo.lat != null && selfGeo.lon != null) {
      selfPos = peerMapProject(selfGeo.lat, selfGeo.lon, w, h);
    }

    // peer markers (store positions for hover hit-testing)
    peerMapMarkers = [];
    points.forEach(function (p) {
      if (p.unknown) return;
      var pos = peerMapProject(p.lat, p.lon, w, h);
      peerMapMarkers.push({ x: pos.x, y: pos.y, ip: p.ip, city: p.city, country: p.country });
    });

    // data-flow arcs (flight-map style): quadratic curves between the self
    // star and each peer, with an animated dash pattern expressing the
    // direction of the connection. outbound: self -> peer (orange);
    // inbound: peer -> self (blue).
    if (selfPos) {
      peerMapMarkers.forEach(function (m) {
        if (!m.ip) return;
        var inbound = !!peerMapInbound[m.ip];
        var x1 = selfPos.x, y1 = selfPos.y, x2 = m.x, y2 = m.y;
        if (inbound) { var tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
        peerMapDrawArc(ctx, x1, y1, x2, y2, t, inbound ? "#58a6ff" : "#f7931a");
      });
    }

    // peer dots on top of the arcs
    peerMapMarkers.forEach(function (m) {
      ctx.beginPath();
      ctx.arc(m.x, m.y, 6, 0, Math.PI * 2);
      ctx.fillStyle = "#f7931a";
      ctx.fill();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = "rgba(255, 255, 255, 0.85)";
      ctx.stroke();
    });

    // this node's own location: solid green star
    if (selfPos) {
      peerMapMarkers.push({ x: selfPos.x, y: selfPos.y, ip: "", city: selfGeo.city, country: selfGeo.country, self: true });
      peerMapDrawStar(ctx, selfPos.x, selfPos.y, 9, "#ff3b30", "rgba(255, 255, 255, 0.9)");
    }
  }

  /* Flight-map style arc: quadratic bezier bulging upward, dashed with a
   * scrolling dash offset so the flow visibly travels from (x1,y1) to
   * (x2,y2). A small arrowhead marks the destination end. */
  function peerMapDrawArc(ctx, x1, y1, x2, y2, t, color) {
    var dx = x2 - x1, dy = y2 - y1;
    var dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < 30) return; // endpoints nearly overlap: arc would be noise
    var mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
    var nx = -dy / dist, ny = dx / dist;
    if (ny > 0) { nx = -nx; ny = -ny; } // prefer the upward-bulging side
    var cx = mx + nx * dist * 0.18, cy = my + ny * dist * 0.18;

    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.quadraticCurveTo(cx, cy, x2, y2);
    ctx.setLineDash([5, 9]);
    ctx.lineDashOffset = -t * 28; // scrolls dashes toward (x2, y2)
    ctx.strokeStyle = color;
    ctx.lineWidth = 1.4;
    ctx.globalAlpha = 0.9;
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.globalAlpha = 1;

    // arrowhead at the destination, tangent = control point -> end
    var ang = Math.atan2(y2 - cy, x2 - cx);
    var s = 7;
    ctx.beginPath();
    ctx.moveTo(x2, y2);
    ctx.lineTo(x2 - s * Math.cos(ang - 0.42), y2 - s * Math.sin(ang - 0.42));
    ctx.lineTo(x2 - s * Math.cos(ang + 0.42), y2 - s * Math.sin(ang + 0.42));
    ctx.closePath();
    ctx.fillStyle = color;
    ctx.fill();
  }

  function peerMapDrawCanvas(points) {
    peerMapPoints = points;
    var api = lastDashboardApi;
    var detail = api && api.bitcoin && Array.isArray(api.bitcoin.peers_detail) ? api.bitcoin.peers_detail : [];
    peerMapInbound = {};
    detail.forEach(function (d) {
      if (d && typeof d.ip === "string" && d.inbound) peerMapInbound[d.ip] = true;
    });
    peerMapDrawFrame(points, 0);
  }

  function peerMapStartAnim() {
    if (peerMapAnimId != null) return;
    var start = performance.now();
    var frame = function (now) {
      peerMapDrawFrame(peerMapPoints, (now - start) / 1000);
      peerMapAnimId = requestAnimationFrame(frame);
    };
    peerMapAnimId = requestAnimationFrame(frame);
  }

  function peerMapStopAnim() {
    if (peerMapAnimId != null) {
      cancelAnimationFrame(peerMapAnimId);
      peerMapAnimId = null;
    }
  }

  function peerMapDrawStar(ctx, cx, cy, r, fill, stroke) {
    ctx.beginPath();
    for (var i = 0; i < 10; i++) {
      var rad = (i % 2 === 0) ? r : r * 0.45;
      var a = -Math.PI / 2 + (i * Math.PI) / 5;
      var x = cx + rad * Math.cos(a), y = cy + rad * Math.sin(a);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = stroke;
    ctx.stroke();
  }

  function peerMapOnMouseMove(e) {
    var canvas = document.getElementById("peerMapCanvas");
    var tooltip = document.getElementById("peerMapTooltip");
    if (!canvas || !tooltip) return;
    var rect = canvas.getBoundingClientRect();
    var scaleX = canvas.width / rect.width;
    var scaleY = canvas.height / rect.height;
    var mx = (e.clientX - rect.left) * scaleX;
    var my = (e.clientY - rect.top) * scaleY;
    var hit = null;
    for (var i = 0; i < peerMapMarkers.length; i++) {
      var m = peerMapMarkers[i];
      var dx = mx - m.x, dy = my - m.y;
      if (dx * dx + dy * dy <= 160) { hit = m; break; } // ~13px in canvas coords
    }
    if (hit) {
      tooltip.hidden = false;
      var loc = [hit.city, hit.country].filter(Boolean).join(", ");
      tooltip.textContent = hit.self ? (loc || t("peerMap.self")) : (hit.ip + "  ·  " + loc);
      var tipX = e.clientX - rect.left + 14;
      var tipY = e.clientY - rect.top - 32;
      if (tipX + 180 > rect.width) tipX = e.clientX - rect.left - 190;
      if (tipY < 0) tipY = e.clientY - rect.top + 14;
      tooltip.style.left = tipX + "px";
      tooltip.style.top = tipY + "px";
      canvas.style.cursor = "pointer";
    } else {
      tooltip.hidden = true;
      canvas.style.cursor = "";
    }
  }

  function peerMapOnMouseLeave() {
    var tooltip = document.getElementById("peerMapTooltip");
    if (tooltip) tooltip.hidden = true;
    var canvas = document.getElementById("peerMapCanvas");
    if (canvas) canvas.style.cursor = "";
  }

  function peerMapResolve(ips) {
    var pending = ips.filter(function (ip) { return !peerMapGeoCache[ip]; });
    var done = function () {
      var points = ips.map(function (ip) {
        var g = peerMapGeoCache[ip];
        if (g && !g.unknown) return { ip: ip, lat: g.lat, lon: g.lon, city: g.city, country: g.country };
        return { ip: ip, unknown: true };
      });
      peerMapDrawCanvas(points);
      var located = points.filter(function (p) { return !p.unknown; }).length;
      setText("peerMapStatus", t("peerMap.status").replace("{n}", String(located)).replace("{total}", String(ips.length)));
    };
    if (!pending.length) { done(); return; }
    // Local offline lookup via /api/geoip.cgi (GeoLite2-City.mmdb on the
    // board). No external network needed; works fully offline.
    var CHUNK = 200; // CGI caps at 200 IPs per request
    var allDone = 0;
    for (var i = 0; i < pending.length; i += CHUNK) {
      (function (chunk) {
        fetch("/api/geoip.cgi?ips=" + encodeURIComponent(chunk.join(",")))
          .then(function (r) { return r.json(); })
          .then(function (data) {
            var results = (data && data.ok) ? (data.results || {}) : {};
            chunk.forEach(function (ip) {
              var row = results[ip];
              if (row && row.lat != null) {
                peerMapGeoCache[ip] = { lat: row.lat, lon: row.lon, country: row.country, city: row.city };
              } else {
                peerMapGeoCache[ip] = { unknown: true };
              }
            });
          })
          .catch(function () {
            chunk.forEach(function (ip) { peerMapGeoCache[ip] = { unknown: true }; });
          })
          .then(function () {
            allDone++;
            if (allDone * CHUNK >= pending.length) done();
          });
      })(pending.slice(i, i + CHUNK));
    }
  }

  function openPeerMap() {
    var overlay = document.getElementById("peerMapOverlay");
    if (!overlay) return;
    var ips = peerMapGetIps();
    overlay.hidden = false;
    setText("peerMapStatus", t("peerMap.loading"));
    if (!ips.length) {
      peerMapDrawCanvas([]);
      setText("peerMapStatus", t("peerMap.empty"));
      return;
    }
    peerMapResolve(ips);
    peerMapStartAnim();
  }

  function closePeerMap() {
    peerMapStopAnim();
    var overlay = document.getElementById("peerMapOverlay");
    if (overlay) overlay.hidden = true;
  }

  function bindPeerMap() {
    var trigger = document.getElementById("nodeConn");
    if (trigger) {
      trigger.addEventListener("click", openPeerMap);
      trigger.addEventListener("keydown", function (e) {
        if (e.key === "Enter" || e.key === " ") { e.preventDefault(); openPeerMap(); }
      });
    }
    var closeBtn = document.getElementById("peerMapCloseBtn");
    if (closeBtn) closeBtn.addEventListener("click", closePeerMap);
    var overlay = document.getElementById("peerMapOverlay");
    if (overlay) overlay.addEventListener("click", function (e) {
      if (e.target === overlay) closePeerMap();
    });
    var canvas = document.getElementById("peerMapCanvas");
    if (canvas) {
      canvas.addEventListener("mousemove", peerMapOnMouseMove);
      canvas.addEventListener("mouseleave", peerMapOnMouseLeave);
    }
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") closePeerMap();
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

  /* ── Service control (Bitcoind / Stratum start-stop) ────────────────
   * Kebab buttons on the two dashboard cards. Stop asks for confirmation
   * (mining pauses); Start is direct — except Stratum in "waiting" state,
   * which opens a dialog explaining why it cannot start yet (the S62ckpool
   * waiter auto-starts it once bitcoind is ready). Progress is tracked via
   * the regular 30s status refresh; a fast poll is started right after an
   * action so the button label flips promptly. */
  var svcStopTarget = null; // "bitcoind" | "ckpool" while the confirm dialog is open
  var svcBusy = { bitcoind: false, ckpool: false };
  var svcPollTimer = null;

  function svcSetBusy(svc, busy) {
    svcBusy[svc] = busy;
    var btn = document.getElementById(svc === "bitcoind" ? "svcBitcoindToggleBtn" : "svcStratumToggleBtn");
    if (btn) btn.disabled = busy;
  }

  function svcStopShow(target) {
    svcStopTarget = target;
    var overlay = document.getElementById("svcStopOverlay");
    var title = document.getElementById("svcStopTitle");
    var note = document.getElementById("svcStopNote");
    if (!overlay || !title || !note) return;
    title.textContent = t(target === "bitcoind" ? "svc.stopBitcoind.title" : "svc.stopStratum.title");
    note.textContent = t(target === "bitcoind" ? "svc.stopBitcoind.warn" : "svc.stopStratum.warn");
    overlay.hidden = false;
  }

  function svcStopHide() {
    svcStopTarget = null;
    var overlay = document.getElementById("svcStopOverlay");
    if (overlay) overlay.hidden = true;
  }

  function svcStartBlockedShow(reason) {
    var overlay = document.getElementById("svcStartBlockedOverlay");
    var el = document.getElementById("svcStartBlockedReason");
    if (!overlay) return;
    if (el) el.textContent = reason || t("svc.startBlocked.fallback");
    overlay.hidden = false;
  }

  function svcStartBlockedHide() {
    var overlay = document.getElementById("svcStartBlockedOverlay");
    if (overlay) overlay.hidden = true;
  }

  function svcStopPollState() {
    return window.fetch("/api/service.cgi?action=state&_ts=" + Date.now(), { cache: "no-store" })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (st) {
        if (!st) return;
        var btcDone = st.bitcoind && st.bitcoind.running === 0;
        var ckDone = st.ckpool && st.ckpool.running === 0;
        if (btcDone && ckDone) {
          window.clearInterval(svcPollTimer);
          svcPollTimer = null;
          svcSetBusy("bitcoind", false);
          svcSetBusy("ckpool", false);
          fetchDashboardData();
        }
      })
      .catch(function () {});
  }

  function svcStartPolling() {
    if (svcPollTimer) return;
    svcPollTimer = window.setInterval(svcStopPollState, 3000);
    svcStopPollState();
  }

  function svcPost(action, svc) {
    return window.fetch("/api/service.cgi?action=" + action + "&service=" + svc, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: ""
    }).then(function (r) { return r.json(); });
  }

  function svcDoStop(svc) {
    svcSetBusy(svc, true);
    svcPost("stop", svc)
      .then(function (res) {
        if (res && res.ok) {
          svcStartPolling();
        } else {
          svcSetBusy(svc, false);
          fetchDashboardData();
        }
      })
      .catch(function () {
        svcSetBusy(svc, false);
        fetchDashboardData();
      });
  }

  function svcDoStart(svc) {
    svcSetBusy(svc, true);
    svcPost("start", svc)
      .then(function (res) {
        if (res && res.ok) {
          svcStartPolling();
        } else if (res && res.error === "not_ready") {
          // only reachable for ckpool when state raced between render and click
          svcSetBusy(svc, false);
          svcStartBlockedShow(res.reason);
          fetchDashboardData();
        } else {
          svcSetBusy(svc, false);
          fetchDashboardData();
        }
      })
      .catch(function () {
        svcSetBusy(svc, false);
        fetchDashboardData();
      });
  }

  function bindServiceControls() {
    var btcBtn = document.getElementById("svcBitcoindToggleBtn");
    if (btcBtn) {
      btcBtn.addEventListener("click", function () {
        var api = lastDashboardApi;
        var running = api && api.bitcoin && api.bitcoin.running === 1;
        if (running) svcStopShow("bitcoind");
        else svcDoStart("bitcoind");
      });
    }
    var stBtn = document.getElementById("svcStratumToggleBtn");
    if (stBtn) {
      stBtn.addEventListener("click", function () {
        var api = lastDashboardApi;
        var state = api && api.mining ? api.mining.ckpool_state : "stopped";
        if (state === "running") {
          svcStopShow("ckpool");
        } else if (state === "waiting") {
          svcStartBlockedShow(api.mining.ckpool_wait_reason || "");
        } else {
          svcDoStart("ckpool");
        }
      });
    }
    var stopCancel = document.getElementById("svcStopCancelBtn");
    if (stopCancel) stopCancel.addEventListener("click", svcStopHide);
    var stopConfirm = document.getElementById("svcStopConfirmBtn");
    if (stopConfirm) {
      stopConfirm.addEventListener("click", function () {
        var target = svcStopTarget;
        svcStopHide();
        if (target) svcDoStop(target);
      });
    }
    var blockedClose = document.getElementById("svcStartBlockedCloseBtn");
    if (blockedClose) blockedClose.addEventListener("click", svcStartBlockedHide);
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
    bindServiceControls();
    bindPeerMap();
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

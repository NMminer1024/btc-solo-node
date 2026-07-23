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
      blockData: "--",
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
      ntpBadge: "...",
      ntpClass: "status-soft",
      ip: "--",
      ssid: "--",
      mac: "--",
      rssi: "--",
      signalLevel: 0
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
    ntp: {
      running: "NTP Ready",
      not_synced: "NTP Unsynced"
    },
    stratum: {
      running: "Running",
      stopped: "Stopped"
    }
  };

  var validViews = {
    dashboard: "Dashboard",
    miners: "Miners",
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
  var DASHBOARD_REFRESH_SECONDS = 10;
  var countdownTimer = null;
  var countdownDeadline = 0;
  var refreshInFlight = false;
  var refreshSecondsLeft = DASHBOARD_REFRESH_SECONDS;
  var LOG_REFRESH_SECONDS = 5;
  var LOG_TAIL_LIMIT = 300;
  var logPollTimer = null;
  var activeViewName = "dashboard";
  var activeLogService = "bitcoind";
  var logRequestSeq = {
    bitcoind: 0,
    stratum: 0
  };
  var logState = {
    bitcoind: {
      cursor: 0,
      text: "",
      loaded: false,
      loading: false,
      path: "/root/bitcoind/debug.log",
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
      state.text = incoming;
    } else if (incoming) {
      state.text += incoming;
    }

    state.cursor = toNumber(payload && payload.cursor, state.cursor);
    state.loaded = true;
    state.path = toText(payload && payload.path, state.path);
    state.label = (payload && payload.service === "stratum") ? "Stratum log" : "Bitcoind log";
    state.loading = false;
    setLogSummary(service, reset ? "Log rotated or truncated · reloaded tail" : summary);
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
    if (num < 1) return num.toPrecision(2) + "%";
    return num.toFixed(2) + "%";
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
    var ntpLabel = labels.ntp[toText(network.ntp_key, "")] || mock.network.ntpBadge;
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
        blockData: system.block_disk_found ? joinParts([system.block_disk_value, system.block_disk_path], mock.system.blockData).replace(" · /", " /") : "Not detected",
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
        ntpBadge: ntpLabel,
        ntpClass: mapStatusClass(toText(network.ntp_class, ""), "status-ok"),
        ip: toText(network.ip_addr, mock.network.ip),
        ssid: toText(network.wifi_ssid, mock.network.ssid),
        mac: toText(network.mac_addr, mock.network.mac),
        rssi: toText(network.rssi_display, mock.network.rssi),
        signalLevel: toNumber(network.signal_level, getRssiLevel(toText(network.rssi_display, mock.network.rssi)))
      },
      about: mock.about
    };
  }

  function getRssiLevel(rssi) {
    if (!rssi && rssi !== 0) return 0;
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    return 1;
  }

  function getRssiColor(rssi) {
    if (!rssi && rssi !== 0) return "#444";
    if (rssi >= -50) return "#4CAF50";
    if (rssi >= -60) return "#8BC34A";
    if (rssi >= -70) return "#FF9800";
    return "#f44336";
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

    setBadgeState("syncBadge", data.bitcoin.syncBadge, data.bitcoin.syncClass);
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
    setRollingValue("nodeUptime", data.bitcoin.uptime);

    var orb = document.getElementById("ibdOrb");
    if (orb) orb.style.setProperty("--pct", data.bitcoin.progressPct);

    setBadgeState("stratumBadge", data.stratum.badge, data.stratum.badgeClass);
    setText("stratumUrl", data.stratum.url);
    setText("stratumUser", data.stratum.user);
    setText("stratumPass", data.stratum.pass);
    setText("stratumMode", data.stratum.mode);

    setText("sysCpu", data.system.cpuModel);
    setText("sysTemp", data.system.temperature);
    setText("sysBlock", data.system.blockData);
    setText("sysRun", data.system.uptime);

    setBadgeState("ntpBadge", data.network.ntpBadge, data.network.ntpClass);
    setText("netIp", data.network.ip);
    setText("netSsid", data.network.ssid);
    setText("netMac", data.network.mac);
    setText("netRssi", data.network.rssi);

    var rssiBars = document.querySelectorAll("[data-rssi-bar]");
    var rssiLevel = toNumber(data.network.signalLevel, getRssiLevel(data.network.rssi));
    var rssiColor = getRssiColor(parseRssiDbm(data.network.rssi));
    rssiBars.forEach(function (bar, index) {
      bar.style.backgroundColor = index < rssiLevel ? rssiColor : "#444";
    });

    setText("aboutFirmware", data.about.firmware);
    setText("aboutUpdated", data.about.updated);
  }

  function fetchDashboardData() {
    if (refreshInFlight) return Promise.resolve(null);
    refreshInFlight = true;
    return window.fetch("/api/status.cgi?_ts=" + Date.now(), {
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
    });
  }

  function setActiveView(view) {
    var buttons = document.querySelectorAll(".nav-item");
    var panels = document.querySelectorAll(".panel-view");

    buttons.forEach(function (button) {
      button.classList.toggle("is-active", button.getAttribute("data-view") === view);
    });

    panels.forEach(function (panel) {
      panel.classList.toggle("is-visible", panel.getAttribute("data-view-panel") === view);
    });

    activeViewName = view;
    if (view === "logs") startLogPolling(activeLogService || "bitcoind");
    else stopLogPolling();

    window.location.hash = view;
  }

  function bindNav() {
    var buttons = document.querySelectorAll(".nav-item");
    buttons.forEach(function (button) {
      button.addEventListener("click", function () {
        setActiveView(button.getAttribute("data-view"));
      });
    });
  }

  function bindThemeToggle() {
    var toggle = document.getElementById("themeToggle");
    if (!toggle) return;
    toggle.addEventListener("click", function () {
      var current = document.documentElement.getAttribute("data-theme") || "light";
      setTheme(current === "dark" ? "light" : "dark");
    });
  }

  function init() {
    renderDashboard(mock);
    setRefreshCountdown(DASHBOARD_REFRESH_SECONDS);
    bindNav();
    bindLogTabs();
    bindThemeToggle();
    setTheme(getStoredTheme());
    fetchDashboardData();
    startRefreshCountdown();

    var initial = window.location.hash ? window.location.hash.slice(1) : "dashboard";
    initial = viewAliases[initial] || initial;
    if (!validViews[initial]) initial = "dashboard";
    setActiveView(initial);
  }

  init();
})();

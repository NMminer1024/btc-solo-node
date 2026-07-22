(function () {
  var mock = {
    summary: {
      device: "Rockbase K230",
      status: "Healthy",
      updatedAt: "Updated 20s ago",
      syncPct: "99.82%",
      syncText: "IBD nearly complete",
      memory: "2.3G",
      memoryText: "bitcoind RSS",
      storage: "318G",
      storageText: "block data",
      network: "-58 dBm",
      networkText: "WiFi stable"
    },
    system: {
      cpuPct: 31,
      cpuLoad: "1.24 / 4 cores",
      memPct: 48,
      memUsage: "1.9G / 4.0G",
      diskPct: 78,
      diskUsage: "312G / 400G",
      swapPct: 6,
      swapUsage: "64M / 1.0G",
      cpuModel: "Canaan K230 RISC-V",
      temperature: "58.4C",
      blockData: "318G at /root/block",
      uptime: "5d 13h 27m"
    },
    bitcoin: {
      syncBadge: "IBD Syncing",
      pruneBadge: "Full Node",
      progressPct: 99,
      blocks: "902,120 / 903,744",
      bestDate: "Best block date: 2026-07-21",
      context: "main /Satoshi:28.0.0/",
      connections: "14",
      difficulty: "126.7T",
      rss: "2.3G",
      uptime: "5d 13h"
    },
    mining: {
      badge: "Running",
      mode: "Solo",
      address: "bc1qexamplepreview0000000000000000000nm",
      log: "[ckpool] waiting for fresh work template"
    },
    network: {
      ntpBadge: "NTP Ready",
      ip: "192.168.124.15",
      ssid: "Rockbase-Lab",
      mac: "00:11:22:33:44:55",
      rssi: "-58 dBm"
    },
    about: {
      firmware: "mock-fw-20260722",
      updated: "2026-07-22 14:30"
    }
  };

  var pageTitles = {
    dashboard: "Dashboard",
    node: "Node",
    mining: "Mining",
    network: "Network",
    logs: "Logs",
    settings: "Settings",
    about: "About"
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

  function setText(id, value) {
    var el = document.getElementById(id);
    if (el) el.textContent = value;
  }

  function setWidth(id, value) {
    var el = document.getElementById(id);
    if (el) el.style.width = value + "%";
  }

  function getStoredTheme() {
    var theme = "";
    try {
      theme = window.localStorage.getItem("nmTheme") || "";
    } catch (e) {}
    return theme === "dark" ? "dark" : "light";
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

  function renderDashboard() {
    setText("deviceName", mock.summary.device);
    setText("deviceStatus", mock.summary.status);
    setText("updatedAt", mock.summary.updatedAt);
    setText("sumSync", mock.summary.syncPct);
    setText("sumSyncText", mock.summary.syncText);
    setText("sumMemory", mock.summary.memory);
    setText("sumMemoryText", mock.summary.memoryText);
    setText("sumStorage", mock.summary.storage);
    setText("sumStorageText", mock.summary.storageText);
    setText("sumNetwork", mock.summary.network);
    setText("sumNetworkText", mock.summary.networkText);

    setText("cpuLoadPct", mock.system.cpuPct + "%");
    setText("cpuLoadValue", mock.system.cpuLoad);
    setWidth("cpuLoadBar", mock.system.cpuPct);

    setText("memPct", mock.system.memPct + "%");
    setText("memValue", mock.system.memUsage);
    setWidth("memBar", mock.system.memPct);

    setText("diskPct", mock.system.diskPct + "%");
    setText("diskValue", mock.system.diskUsage);
    setWidth("diskBar", mock.system.diskPct);

    setText("swapPct", mock.system.swapPct + "%");
    setText("swapValue", mock.system.swapUsage);
    setWidth("swapBar", mock.system.swapPct);

    setText("syncBadge", mock.bitcoin.syncBadge);
    setText("pruneBadge", mock.bitcoin.pruneBadge);
    setText("ibdPct", mock.bitcoin.progressPct + "%");
    setText("ibdBlocks", mock.bitcoin.blocks);
    setText("ibdDate", mock.bitcoin.bestDate);
    setText("ibdContext", mock.bitcoin.context);
    setText("nodeConn", mock.bitcoin.connections);
    setText("nodeDiff", mock.bitcoin.difficulty);
    setText("nodeRss", mock.bitcoin.rss);
    setText("nodeUptime", mock.bitcoin.uptime);

    var orb = document.getElementById("ibdOrb");
    if (orb) orb.style.setProperty("--pct", mock.bitcoin.progressPct);

    setText("miningBadge", mock.mining.badge);
    setText("miningMode", mock.mining.mode);
    setText("btcAddress", mock.mining.address);
    setText("miningLog", mock.mining.log);

    setText("sysCpu", mock.system.cpuModel);
    setText("sysTemp", mock.system.temperature);
    setText("sysBlock", mock.system.blockData);
    setText("sysRun", mock.system.uptime);

    setText("ntpBadge", mock.network.ntpBadge);
    setText("netIp", mock.network.ip);
    setText("netSsid", mock.network.ssid);
    setText("netMac", mock.network.mac);
    setText("netRssi", mock.network.rssi);

    setText("aboutFirmware", mock.about.firmware);
    setText("aboutUpdated", mock.about.updated);
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

    setText("pageTitle", pageTitles[view] || "Dashboard");
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
    renderDashboard();
    bindNav();
    bindThemeToggle();
    setTheme(getStoredTheme());

    var initial = window.location.hash ? window.location.hash.slice(1) : "dashboard";
    if (!pageTitles[initial]) initial = "dashboard";
    setActiveView(initial);
  }

  init();
})();

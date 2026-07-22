(function () {
  var mock = {
    system: {
      cpuPct: 31,
      cpuLoad: "1.24 / 4 cores",
      memPct: 48,
      memUsage: "1.9G / 4.0G",
      diskPct: 78,
      diskUsage: "312G / 400G",
      cpuModel: "Canaan K230 RISC-V",
      temperature: "58.4C",
      blockData: "318G /root/block",
      uptime: "5d 13h 27m"
    },
    bitcoin: {
      syncBadge: "IBD Syncing",
      pruneBadge: "Pruned Node",
      progressPct: 99,
      blocks: "902,120 / 903,744",
      meta: "Mainnet · v28.0.0 · 2026-07-21",
      connections: "14",
      difficulty: "126.7T",
      rss: "2.3G",
      uptime: "5d 13h"
    },
    stratum: {
      badge: "Running",
      url: "stratum+tcp://192.168.124.15:3333",
      user: "wallet.workername",
      pass: "(leave blank)",
      mode: "LAN miners -> local Stratum"
    },
    network: {
      ntpBadge: "NTP Ready",
      ip: "192.168.124.15",
      ssid: "Rockbase-Lab",
      mac: "00:11:22:33:44:55",
      rssi: "-58 dBm",
      reach: "192.168.124.15:3333"
    },
    about: {
      firmware: "mock-fw-20260722",
      updated: "2026-07-22 14:30"
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

  function setText(id, value) {
    var el = document.getElementById(id);
    if (el) el.textContent = value;
  }

  function setProgress(id, pct) {
    var el = document.getElementById(id);
    if (!el) return;
    el.style.setProperty("--pct", pct);
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

  function renderDashboard() {
    setText("cpuPct", mock.system.cpuPct + "%");
    setText("cpuLoadValue", mock.system.cpuLoad);
    setText("memPct", mock.system.memPct + "%");
    setText("memValue", mock.system.memUsage);
    setText("diskPct", mock.system.diskPct + "%");
    setText("diskValue", mock.system.diskUsage);
    setProgress("cpuOrb", mock.system.cpuPct);
    setProgress("memOrb", mock.system.memPct);
    setProgress("diskOrb", mock.system.diskPct);

    setText("syncBadge", mock.bitcoin.syncBadge);
    setText("pruneBadge", mock.bitcoin.pruneBadge);
    setText("ibdPct", mock.bitcoin.progressPct + "%");
    setText("ibdBlocks", mock.bitcoin.blocks);
    setText("ibdMeta", mock.bitcoin.meta);
    setText("nodeConn", mock.bitcoin.connections);
    setText("nodeDiff", mock.bitcoin.difficulty);
    setText("nodeRss", mock.bitcoin.rss);
    setText("nodeUptime", mock.bitcoin.uptime);

    var orb = document.getElementById("ibdOrb");
    if (orb) orb.style.setProperty("--pct", mock.bitcoin.progressPct);

    setText("stratumBadge", mock.stratum.badge);
    setText("stratumUrl", mock.stratum.url);
    setText("stratumUser", mock.stratum.user);
    setText("stratumPass", mock.stratum.pass);
    setText("stratumMode", mock.stratum.mode);

    setText("sysCpu", mock.system.cpuModel);
    setText("sysTemp", mock.system.temperature);
    setText("sysBlock", mock.system.blockData);
    setText("sysRun", mock.system.uptime);

    setText("ntpBadge", mock.network.ntpBadge);
    setText("netIp", mock.network.ip);
    setText("netSsid", mock.network.ssid);
    setText("netMac", mock.network.mac);
    setText("netRssi", mock.network.rssi);
    setText("stratumReach", mock.network.reach);

    var rssiBars = document.querySelectorAll("[data-rssi-bar]");
    var rssiLevel = getRssiLevel(mock.network.rssi);
    var rssiColor = getRssiColor(mock.network.rssi);
    rssiBars.forEach(function (bar, index) {
      bar.style.backgroundColor = index < rssiLevel ? rssiColor : "#444";
    });

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
    initial = viewAliases[initial] || initial;
    if (!validViews[initial]) initial = "dashboard";
    setActiveView(initial);
  }

  init();
})();

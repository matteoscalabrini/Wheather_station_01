const grid = document.querySelector("#displayGrid");
const solarMode = document.querySelector("#solarMode");
const wifiState = document.querySelector("#wifiState");
const postState = document.querySelector("#postState");
const remoteState = document.querySelector("#remoteState");
const menuButton = document.querySelector("#menuButton");
const menuPanel = document.querySelector("#menuPanel");
const dashboardView = document.querySelector("#dashboardView");
const adminView = document.querySelector("#adminView");
const loginForm = document.querySelector("#loginForm");
const adminPassword = document.querySelector("#adminPassword");
const adminState = document.querySelector("#adminState");
const adminPanel = document.querySelector("#adminPanel");
const configForm = document.querySelector("#configForm");
const scanWifi = document.querySelector("#scanWifi");
const scanStatus = document.querySelector("#scanStatus");
const networkList = document.querySelector("#networkList");
const wifiHint = document.querySelector("#wifiHint");
const clearWifiPassword = document.querySelector("#clearWifiPassword");
const clearWifiCache = document.querySelector("#clearWifiCache");
const postNow = document.querySelector("#postNow");
const postHint = document.querySelector("#postHint");
const otaForm = document.querySelector("#otaForm");
const fsOtaForm = document.querySelector("#fsOtaForm");
let password = "";

function showView(name) {
  dashboardView.classList.toggle("hidden", name !== "dashboard");
  adminView.classList.toggle("hidden", name !== "admin");
  menuPanel.classList.add("hidden");
  menuButton.setAttribute("aria-expanded", "false");
}

function fmt(value, unit) {
  if (value === null || value === undefined) return `-- ${unit || ""}`.trim();
  return `${value} ${unit || ""}`.trim();
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function rssiToStrength(rssi) {
  if (rssi >= -50) return 4;
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  return 1;
}

function prettyState(value) {
  return String(value || "--").replace(/_/g, " ").toUpperCase();
}

function elapsedSince(nowMs, eventMs) {
  if (!nowMs || !eventMs) return "";
  const age = Math.max(0, Math.floor((nowMs - eventMs) / 1000));
  if (age < 60) return `${age}s ago`;
  const minutes = Math.floor(age / 60);
  if (minutes < 60) return `${minutes}m ago`;
  return `${Math.floor(minutes / 60)}h ago`;
}

function setHint(element, text, state = "") {
  if (!element) return;
  element.textContent = text;
  element.classList.toggle("connected", state === "connected");
  element.classList.toggle("warn", state === "warn");
  element.classList.toggle("bad", state === "bad");
}

function setWifiHint(text, state = "") {
  setHint(wifiHint, text, state);
}

function setPostHint(text, state = "") {
  setHint(postHint, text, state);
}

function updateWifiPasswordMode() {
  if (!clearWifiPassword || !configForm?.elements.wifiPassword) return;
  const passwordField = configForm.elements.wifiPassword;
  passwordField.disabled = clearWifiPassword.checked;
  if (clearWifiPassword.checked) {
    passwordField.value = "";
    passwordField.placeholder = "open network";
  } else {
    passwordField.placeholder = "leave blank to keep";
  }
}

function renderNetworkList(networks) {
  networkList.innerHTML = "";
  if (!networks || networks.length === 0) {
    networkList.innerHTML = "<div class='no-networks'>No networks found</div>";
    return;
  }
  networks.forEach((net) => {
    const strength = rssiToStrength(net.rssi);
    const ssid = net.ssid || "(hidden)";
    const item = document.createElement("div");
    item.className = "network-item";
    item.innerHTML = `
      <div class="net-info">
        <span class="net-ssid">${escapeHtml(ssid)}</span>
        <span class="net-meta">${net.rssi}&nbsp;dBm &middot; ${net.secure ? "SECURED" : "OPEN"}</span>
      </div>
      <div class="net-bars" data-bars="${strength}" aria-label="Signal: ${strength} of 4">
        <span></span><span></span><span></span><span></span>
      </div>
      <button type="button" class="net-connect">Connect</button>
    `;
    item.querySelector(".net-connect").addEventListener("click", () => {
      configForm.elements.wifiSsid.value = net.ssid || "";
      configForm.elements.wifiPassword.value = "";
      if (clearWifiPassword) {
        clearWifiPassword.checked = !net.secure;
        updateWifiPasswordMode();
      }
      if (net.secure) {
        configForm.elements.wifiPassword.focus();
      } else {
        scanStatus.textContent = `${ssid} selected as open network`;
      }
    });
    networkList.appendChild(item);
  });
}

function renderWifiStatus(data) {
  const wifi = data.wifi || {};
  const mode = String(data.solarMode || "").toLowerCase();
  const target = wifi.targetSsid || "";
  const message = prettyState(wifi.message || wifi.statusLabel);

  if (wifi.sta) {
    wifiState.textContent = `STA ${wifi.ip}`;
    const ssid = wifi.ssid || target || "WiFi";
    const rssi = wifi.rssi ? ` RSSI ${wifi.rssi} dBm` : "";
    setWifiHint(`Connected to ${ssid} at ${wifi.ip}.${rssi}`, "connected");
    return;
  }

  if (wifi.enabled && target && (wifi.message === "wifi_connecting" || wifi.message === "wifi_retry_wait")) {
    wifiState.textContent = "WIFI CONNECTING";
    setWifiHint(`Trying ${target}. ${message}`, "warn");
    return;
  }

  if (wifi.recoveryAp && wifi.ap) {
    const minutes = Math.max(1, Math.ceil(Number(wifi.recoveryApRemainingMs || 0) / 60000));
    wifiState.textContent = `AP ${wifi.apIp}`;
    setWifiHint(`Recovery AP active for ${minutes}m after ${target} was unreachable. Update WiFi settings or use Post Now to test.`, "bad");
    return;
  }

  if (target && (mode === "shadow" || mode === "dark")) {
    wifiState.textContent = wifi.ap ? `AP ${wifi.apIp}` : "STA IDLE";
    setWifiHint(`Saved ${target}. Station WiFi is idle in ${mode} mode until a post is due. ${message}`, "warn");
    return;
  }

  if (target) {
    wifiState.textContent = wifi.enabled ? "WIFI WAIT" : "WIFI SAVED";
    setWifiHint(`Saved ${target}. ${message}`, wifi.message === "wifi_connect_timeout" ? "bad" : "warn");
    return;
  }

  if (wifi.ap) {
    wifiState.textContent = `AP ${wifi.apIp}`;
    setWifiHint(`Setup AP active at ${wifi.apIp}. No station WiFi saved.`, "warn");
    return;
  }

  wifiState.textContent = wifi.enabled ? "WIFI ON" : "WIFI OFF";
  setWifiHint(wifi.enabled ? `WiFi enabled. ${message}` : "WiFi is off.");
}

function renderPostStatus(data) {
  const wifi = data.wifi || {};
  const code = Number(wifi.lastPostCode || 0);
  const message = wifi.lastPostMessage || "idle";
  const age = elapsedSince(Number(data.uptimeMs || 0), Number(wifi.lastPostSuccessMs || 0));

  if (wifi.posting) {
    postState.textContent = "POST SENDING";
    setPostHint(`Post in progress. ${prettyState(message)}`, "warn");
    return;
  }

  if (message === "post_ok") {
    postState.textContent = `POST OK ${code || ""}`.trim();
    setPostHint(`Last post succeeded${age ? ` ${age}` : ""}. HTTP ${code}.`, "connected");
    return;
  }

  if (message && message !== "idle") {
    postState.textContent = code ? `POST ${code}` : "POST FAILED";
    const detail = code ? ` HTTP ${code}.` : "";
    setPostHint(`${prettyState(message)}.${detail}`, "bad");
    return;
  }

  postState.textContent = "POST IDLE";
  setPostHint("No post attempted yet.");
}

function renderRemoteStatus(data) {
  const wifi = data.wifi || {};
  const configMessage = wifi.remoteConfigMessage || "idle";
  const firmwareMessage = wifi.firmwareMessage || "idle";
  const active = [
    "remote_config_fetching",
    "firmware_checking",
    "firmware_downloading",
    "firmware_rebooting",
    "spiffs_downloading",
    "spiffs_rebooting"
  ];
  const ok = ["remote_config_saved", "remote_config_current", "remote_config_empty", "firmware_current"];

  if (active.includes(configMessage) || active.includes(firmwareMessage)) {
    remoteState.textContent = `REMOTE ${prettyState(firmwareMessage !== "idle" ? firmwareMessage : configMessage)}`;
    return;
  }
  if (firmwareMessage.includes("failed") || firmwareMessage.includes("mismatch") ||
      firmwareMessage.includes("missing") || firmwareMessage.includes("unauthorized") ||
      configMessage.includes("failed") || configMessage.includes("invalid") ||
      configMessage.includes("unauthorized")) {
    remoteState.textContent = "REMOTE CHECK";
    return;
  }
  if (ok.includes(configMessage) || ok.includes(firmwareMessage)) {
    remoteState.textContent = "REMOTE OK";
    return;
  }
  remoteState.textContent = "REMOTE IDLE";
}

function renderStatus(data) {
  solarMode.textContent = `MODE ${String(data.solarMode || "--").toUpperCase()}`;
  renderWifiStatus(data);
  renderPostStatus(data);
  renderRemoteStatus(data);

  grid.innerHTML = "";
  (data.displays || []).forEach((item) => {
    const tile = document.createElement("article");
    tile.className = "tile";
    const secondary = item.secondaryLabel ?
      `${item.secondaryLabel} ${fmt(item.secondary, item.secondaryUnit)}` :
      fmt(item.secondary, item.secondaryUnit);
    tile.innerHTML = `
      <header><span>${item.label}</span><span class="${item.online ? "online" : "offline"}">${item.online ? "LIVE" : "OFF"}</span></header>
      <div class="value">${fmt(item.primary, item.primaryUnit)}</div>
      <div class="secondary">${secondary || ""}</div>
    `;
    grid.appendChild(tile);
  });
}

menuButton.addEventListener("click", () => {
  const nextOpen = menuPanel.classList.contains("hidden");
  menuPanel.classList.toggle("hidden", !nextOpen);
  menuButton.setAttribute("aria-expanded", nextOpen ? "true" : "false");
});

menuPanel.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-view]");
  if (!button) return;
  showView(button.dataset.view);
});

async function refreshStatus() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    renderStatus(await response.json());
  } catch {
    wifiState.textContent = "LINK LOST";
    setWifiHint("Dashboard cannot reach the device.", "bad");
    postState.textContent = "LINK LOST";
    setPostHint("Dashboard cannot reach the device.", "bad");
    remoteState.textContent = "LINK LOST";
  }
}

function withPassword(params = new URLSearchParams()) {
  params.set("password", password);
  return params;
}

async function loadConfig() {
  const response = await fetch(`/api/admin/config?${withPassword()}`, { cache: "no-store" });
  if (!response.ok) throw new Error("unauthorized");
  const config = await response.json();
  for (const [key, value] of Object.entries(config)) {
    const field = configForm.elements[key];
    if (!field) continue;
    if (field.type === "checkbox") field.checked = !!value;
    else field.value = value ?? "";
  }
  if (clearWifiPassword) {
    clearWifiPassword.checked = Boolean(config.wifiSsid) && !config.wifiPasswordSet;
    updateWifiPasswordMode();
  }
}

loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  password = adminPassword.value;
  try {
    await loadConfig();
    adminPanel.classList.remove("hidden");
    adminState.textContent = "Unlocked";
  } catch {
    adminState.textContent = "Denied";
  }
});

configForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  adminState.textContent = "Saving...";
  setWifiHint("Saving WiFi/settings changes...", "warn");
  const params = withPassword(new URLSearchParams());
  for (const element of configForm.elements) {
    if (!element.name) continue;
    if (element.type === "checkbox") params.set(element.name, element.checked ? "1" : "0");
    else if (element.value !== "") params.set(element.name, element.value);
  }
  try {
    const response = await fetch("/api/admin/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: params
    });
    adminState.textContent = response.ok ? "Saved" : "Save failed";
    if (response.ok) {
      await loadConfig();
      await refreshStatus();
    } else {
      setWifiHint("Settings save failed.", "bad");
    }
  } catch {
    adminState.textContent = "Save error";
    setWifiHint("Settings save request failed.", "bad");
  }
});

scanWifi.addEventListener("click", async () => {
  scanStatus.textContent = "Scanning…";
  scanWifi.disabled = true;
  networkList.innerHTML = "";
  try {
    const response = await fetch("/api/admin/wifi-scan", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: withPassword()
    });
    const data = await response.json();
    const networks = data.networks || [];
    renderNetworkList(networks);
    scanStatus.textContent = `${networks.length} network${networks.length !== 1 ? "s" : ""} found`;
  } catch {
    scanStatus.textContent = "Scan failed";
    networkList.innerHTML = "";
  } finally {
    scanWifi.disabled = false;
  }
});

postNow.addEventListener("click", async () => {
  postNow.disabled = true;
  postState.textContent = "POST SENDING";
  setPostHint("Connecting WiFi and sending telemetry now...", "warn");
  try {
    const response = await fetch("/api/admin/post-now", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: withPassword()
    });
    const data = await response.json();
    if (data.telemetry || data.displays) renderStatus(data.telemetry || data);
    if (!response.ok) {
      postState.textContent = "POST FAILED";
      setPostHint(`Manual post failed. ${prettyState(data.wifi?.lastPostMessage || data.error || "post_failed")}.`, "bad");
    }
  } catch {
    postState.textContent = "POST ERROR";
    setPostHint("Manual post request failed.", "bad");
  } finally {
    postNow.disabled = false;
    await refreshStatus();
  }
});

clearWifiCache?.addEventListener("click", async () => {
  if (!confirm("Clear saved station WiFi SSID/password and reset WiFi cache?")) return;
  clearWifiCache.disabled = true;
  scanStatus.textContent = "Clearing WiFi cache...";
  setWifiHint("Clearing saved station WiFi and driver cache...", "warn");
  try {
    const response = await fetch("/api/admin/wifi-clear-cache", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: withPassword()
    });
    if (!response.ok) throw new Error("clear_failed");
    configForm.elements.wifiSsid.value = "";
    configForm.elements.wifiPassword.value = "";
    if (clearWifiPassword) clearWifiPassword.checked = false;
    updateWifiPasswordMode();
    networkList.innerHTML = "";
    scanStatus.textContent = "WiFi cache cleared";
    await loadConfig();
    await refreshStatus();
  } catch {
    scanStatus.textContent = "Clear failed";
    setWifiHint("WiFi cache clear failed.", "bad");
  } finally {
    clearWifiCache.disabled = false;
  }
});

async function uploadOta(fileInputId, endpoint, stateSelector) {
  const stateEl = document.querySelector(stateSelector);
  const file = document.querySelector(fileInputId).files[0];
  if (!file) {
    stateEl.textContent = "Choose a file first";
    return;
  }
  const body = new FormData();
  body.append("update", file);
  stateEl.textContent = "Uploading…";
  try {
    const response = await fetch(`${endpoint}?password=${encodeURIComponent(password)}`, {
      method: "POST",
      body
    });
    stateEl.textContent = response.ok ? "Done — rebooting" : "Upload failed";
  } catch {
    stateEl.textContent = "Upload error";
  }
}

otaForm.addEventListener("submit", (event) => {
  event.preventDefault();
  uploadOta("#otaFile", "/api/ota/upload", "#otaState");
});

fsOtaForm.addEventListener("submit", (event) => {
  event.preventDefault();
  uploadOta("#fsOtaFile", "/api/ota/upload-spiffs", "#fsOtaState");
});

clearWifiPassword?.addEventListener("change", updateWifiPasswordMode);
updateWifiPasswordMode();
refreshStatus();
setInterval(refreshStatus, 5000);

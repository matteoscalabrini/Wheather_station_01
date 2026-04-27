const grid = document.querySelector("#displayGrid");
const solarMode = document.querySelector("#solarMode");
const wifiState = document.querySelector("#wifiState");
const postState = document.querySelector("#postState");
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
const postNow = document.querySelector("#postNow");
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
      configForm.elements.wifiPassword.focus();
    });
    networkList.appendChild(item);
  });
}

function renderStatus(data) {
  solarMode.textContent = `MODE ${String(data.solarMode || "--").toUpperCase()}`;
  wifiState.textContent = data.wifi?.ap ? `AP ${data.wifi.apIp}` :
    data.wifi?.sta ? `STA ${data.wifi.ip}` : "WIFI OFF";
  postState.textContent = data.wifi?.lastPostMessage ?
    `POST ${data.wifi.lastPostMessage}` : "POST --";

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
    postState.textContent = "LINK LOST";
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
  const params = withPassword(new URLSearchParams());
  for (const element of configForm.elements) {
    if (!element.name) continue;
    if (element.type === "checkbox") params.set(element.name, element.checked ? "1" : "0");
    else if (element.value !== "") params.set(element.name, element.value);
  }
  const response = await fetch("/api/admin/config", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: params
  });
  adminState.textContent = response.ok ? "Saved" : "Save failed";
  if (response.ok) await loadConfig();
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
  const response = await fetch("/api/admin/post-now", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: withPassword()
  });
  postState.textContent = response.ok ? "POST REQUESTED" : "POST FAILED";
  await refreshStatus();
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

refreshStatus();
setInterval(refreshStatus, 5000);

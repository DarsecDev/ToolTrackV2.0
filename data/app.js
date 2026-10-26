/**
 * ToolTrack — interfaz web
 * Conecta la UI con la API REST y el WebSocket del ESP32-S3.
 */

const TOOL_COUNT = 12;
const GRID_COLS = 3;
const HISTORY_PAGE_SIZE = 50;

const ACTION_LABELS = {
  taken: "Tomada",
  returned: "Devuelta",
  mismatch: "Discrepancia",
  access_granted: "Acceso autorizado",
  access_denied: "Acceso denegado",
  camera_offline: "Cámara sin conexión",
  power_lost: "Corte de red",
  power_restored: "Red restaurada",
  unknown: "Desconocido",
};

const SOURCE_LABELS = {
  ir: "IR",
  camera: "Cámara",
  rfid: "RFID",
  system: "Sistema",
};

const ALERT_LABELS = {
  access_denied: "Acceso denegado: tarjeta no autorizada",
  taken_without_session: "Herramienta retirada sin sesión activa",
  ir_camera_mismatch: "El IR y la cámara no coinciden",
  tool_wrong_slot: "La cámara detectó una herramienta en la ranura equivocada",
};

const state = {
  tools: [],
  status: {},
  events: [],
  eventsTotal: 0,
  eventsOffset: 0,
  historyLoaded: false,
  session: { authenticated: false, mustChangePassword: false },
  admin: { users: [], tools: [], settings: {} },
  adminLoaded: false,
  photoMeta: null,
  ws: null,
  wsRetryTimer: null,
  busy: { status: false, tools: false, photo: false },
};

function byId(id) {
  return document.getElementById(id);
}

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (character) => {
    switch (character) {
      case "&":
        return "&amp;";
      case "<":
        return "&lt;";
      case ">":
        return "&gt;";
      case '"':
        return "&quot;";
      default:
        return "&#39;";
    }
  });
}

function slotForId(id) {
  if (!Number.isInteger(id) || id < 0 || id >= TOOL_COUNT) return "--";
  return `${String.fromCharCode(65 + Math.floor(id / GRID_COLS))}${(id % GRID_COLS) + 1}`;
}

function formatEpoch(epoch) {
  const value = Number(epoch);
  if (!value) return "—";
  return new Date(value * 1000).toLocaleString("es-MX", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function formatIso(iso) {
  if (!iso) return "Hora desconocida";
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return iso;
  return date.toLocaleString("es-MX", {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function formatUptime(seconds) {
  const total = Number(seconds) || 0;
  const days = Math.floor(total / 86400);
  const hours = Math.floor((total % 86400) / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  if (days > 0) return `${days}d ${hours}h ${minutes}m`;
  if (hours > 0) return `${hours}h ${minutes}m`;
  return `${minutes}m ${Math.floor(total % 60)}s`;
}

function setText(id, value) {
  const element = byId(id);
  if (element) element.textContent = value;
}

async function apiFetch(path, options = {}) {
  const requestOptions = {
    credentials: "same-origin",
    cache: "no-store",
    ...options,
  };

  if (requestOptions.body !== undefined &&
      typeof requestOptions.body !== "string") {
    requestOptions.headers = {
      ...(requestOptions.headers || {}),
      "Content-Type": "application/json",
    };
    requestOptions.body = JSON.stringify(requestOptions.body);
  }

  const response = await fetch(path, requestOptions);
  const text = await response.text();
  let data = null;
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = { raw: text };
    }
  }

  if (!response.ok) {
    if (response.status === 401) {
      state.session.authenticated = false;
      showAdminLogin();
    }
    if (response.status === 403 && data?.error === "must_change_password") {
      showAdminChangePassword();
    }

    const error = new Error(data?.error || `Error HTTP ${response.status}`);
    error.status = response.status;
    error.data = data;
    throw error;
  }

  return data;
}

function toast(message, type = "info", timeout = 5200) {
  const container = byId("toasts");
  if (!container) return;

  const element = document.createElement("div");
  element.className = `toast toast-${type}`;
  element.textContent = message;
  container.appendChild(element);

  window.setTimeout(() => {
    element.classList.add("is-leaving");
    window.setTimeout(() => element.remove(), 250);
  }, timeout);
}

function markLive(online) {
  const badge = byId("liveBadge");
  if (!badge) return;
  badge.textContent = online ? "En vivo" : "Reconectando";
  badge.classList.toggle("badge-ok", online);
  badge.classList.toggle("badge-danger", !online);
}

function renderClock() {
  const now = new Date();
  setText("clock", now.toLocaleTimeString("es-MX", { hour12: false }));
}

function renderStatus() {
  const status = state.status || {};
  const wifi = status.wifi || {};
  const camera = status.camera || {};
  const time = status.time || {};
  const power = status.power || {};

  setText("statusSystem", status.system || "Sin conexión");
  setText("statusUptime", `Uptime ${formatUptime(status.uptime)}`);
  setText("statusWifi", wifi.connected ? (wifi.ssid || "Conectado") : "Desconectado");
  setText("statusIp", wifi.connected
    ? `${wifi.ip || "—"} · ${Number(wifi.rssi) || 0} dBm`
    : "Sin red");
  setText("statusCamera", camera.online ? "En línea" : "Offline");
  setText("statusCameraDetail",
    camera.online && camera.lastSeen
      ? `Visto ${formatEpoch(camera.lastSeen)}`
      : "Sin conexión");
  setText("statusTaken", `${status.toolsTaken ?? 0} / ${status.toolCount ?? TOOL_COUNT}`);
  setText("statusTakenDetail", `${status.toolsKnown ?? 0} con lectura`);
  setText("statusSession", status.sessionUser || "Sin sesión");
  setText("statusTime",
    time.valid && time.epoch
      ? `Hora: ${formatEpoch(time.epoch)}`
      : "Hora: sin NTP");

  if (status.firmware) setText("firmwareVersion", status.firmware);
  if (power.acPresent === false) {
    setText("statusSystem", `${status.system || "Activo"} · UPS`);
  }

  renderSystemInfo();
}

function renderCabinet() {
  const grid = byId("cabinetGrid");
  if (!grid) return;

  if (!state.tools.length) {
    grid.innerHTML = `
      <div class="empty-state">
        <strong>Sin herramientas configuradas</strong>
        <span>La API todavía no devolvió la matriz del armario.</span>
      </div>`;
    return;
  }

  grid.innerHTML = state.tools.map((tool) => {
    const present = tool.present;
    const variant = present === true ? "is-present" : present === false ? "is-missing" : "is-unknown";
    const dot = present === true ? "dot-present" : present === false ? "dot-missing" : "dot-unknown";
    const stateText = present === true ? "Presente" : present === false ? "Fuera" : "Sin datos";
    const confidence = Number(tool.cameraConfidence) || 0;
    const cameraLine = confidence > 0
      ? `<span class="slot-camera ${tool.cameraPresent === present ? "is-match" : "is-diff"}">Cámara ${confidence}%</span>`
      : "";
    const ownerLine = present === false
      ? `<span class="slot-owner">${escapeHtml(tool.owner || "Sin sesión")}</span>`
      : "";
    const sinceLine = present === false && tool.takenAt
      ? `<span class="slot-since">${escapeHtml(formatEpoch(tool.takenAt))}</span>`
      : "";

    return `
      <article class="slot ${variant}" data-tool-id="${tool.id}">
        <div class="slot-top">
          <span class="slot-code">${escapeHtml(tool.slot || slotForId(tool.id))}</span>
          <span class="slot-state"><i class="dot ${dot}"></i>${stateText}</span>
        </div>
        <span class="slot-name">${escapeHtml(tool.name || `Herramienta ${tool.id + 1}`)}</span>
        <div class="slot-details">
          ${ownerLine}
          ${sinceLine}
          ${cameraLine}
        </div>
      </article>`;
  }).join("");
}

function renderToolOptions() {
  const select = byId("historyTool");
  if (!select) return;

  const current = select.value;
  select.innerHTML = `<option value="">Todas</option>` +
    state.tools.map((tool) =>
      `<option value="${tool.id}">${escapeHtml(tool.slot || slotForId(tool.id))} · ${escapeHtml(tool.name || `Herramienta ${tool.id + 1}`)}</option>`
    ).join("");

  if ([...select.options].some((option) => option.value === current)) {
    select.value = current;
  }
}

async function refreshStatus() {
  if (state.busy.status) return;
  state.busy.status = true;

  try {
    state.status = await apiFetch("/api/status");
    renderStatus();
    markLive(true);
  } catch (error) {
    setText("statusSystem", "Sin conexión con el armario");
    setText("statusUptime", "—");
    markLive(false);
  } finally {
    state.busy.status = false;
  }
}

async function refreshTools() {
  if (state.busy.tools) return;
  state.busy.tools = true;

  try {
    const data = await apiFetch("/api/tools");
    state.tools = Array.isArray(data?.tools) ? data.tools : [];
    renderCabinet();
    renderToolOptions();
    setText("lastUpdate", `Actualizado ${new Date().toLocaleTimeString("es-MX", { hour12: false })}`);
    renderHistory();
  } catch (error) {
    setText("lastUpdate", "Sin conexión");
  } finally {
    state.busy.tools = false;
  }
}

function renderHistory() {
  const body = byId("historyBody");
  if (!body) return;

  const query = (byId("historySearch")?.value || "").trim().toLowerCase();
  const toolFilter = byId("historyTool")?.value || "";

  const filtered = state.events.filter((event) => {
    if (toolFilter !== "" && String(event.toolId) !== String(toolFilter)) return false;
    if (!query) return true;

    const haystack = [
      event.slot,
      event.user,
      event.returnedBy,
      ACTION_LABELS[event.action] || event.action,
      SOURCE_LABELS[event.source] || event.source,
    ].join(" ").toLowerCase();
    return haystack.includes(query);
  });

  if (!filtered.length) {
    body.innerHTML = `
      <tr>
        <td colspan="7" class="table-empty">
          ${state.eventsTotal === 0 ? "Todavía no hay eventos registrados." : "Ningún evento coincide con el filtro."}
        </td>
      </tr>`;
  } else {
    body.innerHTML = filtered.map((event) => {
      const action = event.action || "unknown";
      const actionClass = ["taken", "access_denied", "mismatch", "camera_offline"].includes(action)
        ? "badge-danger"
        : ["returned", "access_granted"].includes(action)
          ? "badge-ok"
          : "badge-muted";
      const confidence = Number(event.confidence) || 0;

      return `
        <tr>
          <td class="nowrap">${escapeHtml(formatIso(event.iso))}</td>
          <td><span class="slot-pill">${escapeHtml(event.slot || "--")}</span></td>
          <td><span class="badge ${actionClass}">${escapeHtml(ACTION_LABELS[action] || action)}</span></td>
          <td>${escapeHtml(event.user || "—")}</td>
          <td>${escapeHtml(event.returnedBy || "—")}</td>
          <td>${escapeHtml(SOURCE_LABELS[event.source] || event.source || "—")}</td>
          <td>${confidence > 0 ? `${confidence}%` : "—"}</td>
        </tr>`;
    }).join("");
  }

  const total = Number(state.eventsTotal) || 0;
  setText("historyCount", `${filtered.length} visibles · ${state.events.length} cargados de ${total}`);

  const moreButton = byId("historyMoreButton");
  if (moreButton) {
    moreButton.disabled = state.events.length >= total || total === 0;
  }
}

async function refreshHistory(reset = false) {
  if (reset) {
    state.events = [];
    state.eventsOffset = 0;
  }

  try {
    const data = await apiFetch(`/api/events?limit=${HISTORY_PAGE_SIZE}&offset=${state.eventsOffset}`);
    const incoming = Array.isArray(data?.events) ? data.events : [];
    const existing = new Set(state.events.map((event) => event.id));
    for (const event of incoming) {
      if (!existing.has(event.id)) {
        state.events.push(event);
        existing.add(event.id);
      }
    }

    state.eventsOffset += incoming.length;
    state.eventsTotal = Number(data?.total) || 0;
    state.historyLoaded = true;
    renderHistory();
  } catch (error) {
    setText("historyCount", "Sin conexión");
  }
}

function renderPhotoMeta(meta) {
  const element = byId("photoMeta");
  if (!element) return;

  if (!meta) {
    element.textContent = "Sin análisis disponible";
    return;
  }

  const parts = [];
  if (meta.modelVersion) parts.push(`Modelo ${meta.modelVersion}`);
  if (meta.processingMs !== undefined) parts.push(`${meta.processingMs} ms`);
  if (meta.capturedAt) parts.push(formatEpoch(meta.capturedAt));
  element.textContent = parts.length ? parts.join(" · ") : "Último análisis recibido";
}

function renderPhotoOverlay(meta, image) {
  const overlay = byId("photoOverlay");
  if (!overlay) return;
  overlay.innerHTML = "";

  const slots = Array.isArray(meta?.slots) ? meta.slots : [];
  if (!slots.length) return;

  const width = Number(meta.width) || image.naturalWidth || 0;
  const height = Number(meta.height) || image.naturalHeight || 0;
  if (!width || !height) return;

  for (const slot of slots) {
    let bbox = slot.bbox;
    if (Array.isArray(bbox)) {
      bbox = { x: bbox[0], y: bbox[1], width: bbox[2], height: bbox[3] };
    }
    if (!bbox || typeof bbox !== "object") continue;

    const x = Number(bbox.x);
    const y = Number(bbox.y);
    const w = Number(bbox.width);
    const h = Number(bbox.height);
    if (![x, y, w, h].every(Number.isFinite)) continue;

    const box = document.createElement("div");
    box.className = `photo-box ${slot.present ? "is-present" : "is-empty"}`;
    box.style.left = `${(x / width) * 100}%`;
    box.style.top = `${(y / height) * 100}%`;
    box.style.width = `${(w / width) * 100}%`;
    box.style.height = `${(h / height) * 100}%`;
    box.innerHTML = `<span>${escapeHtml(slot.slot || "")} · ${Number(slot.confidence) || 0}%</span>`;
    overlay.appendChild(box);
  }
}

async function refreshPhoto() {
  if (state.busy.photo) return;
  state.busy.photo = true;

  try {
    let meta = null;
    try {
      meta = await apiFetch("/api/photo/meta");
    } catch {
      meta = null;
    }
    state.photoMeta = meta;
    renderPhotoMeta(meta);

    const image = byId("lastPhoto");
    const empty = byId("photoEmpty");
    const overlay = byId("photoOverlay");
    if (!image) return;

    image.onload = () => {
      image.hidden = false;
      if (empty) empty.hidden = true;
      renderPhotoOverlay(meta, image);
    };
    image.onerror = () => {
      image.hidden = true;
      if (empty) empty.hidden = false;
      if (overlay) overlay.innerHTML = "";
    };
    image.src = `/api/photo?t=${Date.now()}`;
  } finally {
    state.busy.photo = false;
  }
}

function renderUsers() {
  const body = byId("usersBody");
  if (!body) return;

  const users = state.admin.users || [];
  if (!users.length) {
    body.innerHTML = `<tr><td colspan="6" class="table-empty">No hay usuarios registrados.</td></tr>`;
    return;
  }

  body.innerHTML = users.map((user) => `
    <tr data-user-id="${user.id}">
      <td>${user.id}</td>
      <td class="mono">${escapeHtml(user.uid)}</td>
      <td>${escapeHtml(user.name)}</td>
      <td><span class="badge ${user.active ? "badge-ok" : "badge-muted"}">${user.active ? "Activo" : "Inactivo"}</span></td>
      <td class="nowrap">${user.createdAt ? escapeHtml(formatEpoch(user.createdAt)) : "—"}</td>
      <td class="row-actions">
        <button class="button button-small button-ghost" data-user-edit="${user.id}" type="button">Editar</button>
        <button class="button button-small button-danger" data-user-delete="${user.id}" type="button">Eliminar</button>
      </td>
    </tr>`).join("");
}

function renderAdminTools() {
  const body = byId("toolsBody");
  if (!body) return;

  const tools = state.admin.tools || [];
  if (!tools.length) {
    body.innerHTML = `<tr><td colspan="5" class="table-empty">No hay herramientas configuradas.</td></tr>`;
    return;
  }

  body.innerHTML = tools.map((tool) => `
    <tr data-tool-id="${tool.id}">
      <td><span class="slot-pill">${escapeHtml(tool.slot || slotForId(tool.id))}</span></td>
      <td><input data-field="name" type="text" maxlength="31" value="${escapeHtml(tool.name)}" aria-label="Nombre de ${escapeHtml(tool.slot || slotForId(tool.id))}"></td>
      <td><input data-field="expectedClass" type="text" maxlength="19" value="${escapeHtml(tool.expectedClass || "")}" aria-label="Clase esperada de ${escapeHtml(tool.slot || slotForId(tool.id))}"></td>
      <td class="center"><input data-field="active" type="checkbox" ${tool.active ? "checked" : ""} aria-label="Herramienta activa"></td>
      <td><button class="button button-small" data-tool-save type="button">Guardar</button></td>
    </tr>`).join("");
}

function renderSettings() {
  const settings = state.admin.settings || {};
  const maxEvents = byId("settingMaxEvents");
  const sessionTimeout = byId("settingSessionTimeout");
  const camera = byId("settingCamera");
  const telegram = byId("settingTelegram");
  const token = byId("settingTelegramToken");
  const chat = byId("settingTelegramChat");

  if (maxEvents) maxEvents.value = settings.maxEvents ?? 200;
  if (sessionTimeout) sessionTimeout.value = settings.sessionTimeoutMin ?? 10;
  if (camera) camera.checked = settings.cameraEnabled !== false;
  if (telegram) telegram.checked = settings.telegramEnabled === true;
  if (token) token.value = settings.telegramToken || "";
  if (chat) chat.value = settings.telegramChatId || "";
}

function renderSystemInfo() {
  const container = byId("systemInfo");
  if (!container) return;

  const status = state.status || {};
  const wifi = status.wifi || {};
  const camera = status.camera || {};
  const time = status.time || {};
  const power = status.power || {};

  const items = [
    ["Firmware", status.firmware || "—"],
    ["Boot ID", status.bootId || "—", true],
    ["Uptime", formatUptime(status.uptime)],
    ["WiFi", wifi.connected ? (wifi.ssid || "Conectado") : "Desconectado"],
    ["IP local", wifi.ip || "—", true],
    ["Señal", wifi.connected ? `${Number(wifi.rssi) || 0} dBm` : "—"],
    ["Cámara", camera.online ? "En línea" : "Offline"],
    ["Hora del sistema", time.valid ? formatEpoch(time.epoch) : "Sin NTP"],
    ["Herramientas fuera", `${status.toolsTaken ?? 0} / ${status.toolCount ?? TOOL_COUNT}`],
    ["Sesión", status.sessionUser || "Sin sesión"],
    ["Alimentación", power.acPresent === false ? "UPS / batería" : "Red eléctrica"],
    ["Última conexión cámara", camera.lastSeen ? formatEpoch(camera.lastSeen) : "—"],
    ["Heap libre", status.heap !== undefined ? `${Math.round(status.heap / 1024)} KB` : "—"],
    ["PSRAM libre", status.psram ? `${Math.round(status.psram / 1024)} KB` : "—"],
    ["Eventos en memoria", status.eventsCount ?? "—"],
  ];

  container.innerHTML = items.map(([label, value, mono]) => `
    <div class="info-item">
      <span>${escapeHtml(label)}</span>
      <strong class="${mono ? "mono" : ""}">${escapeHtml(value)}</strong>
    </div>`).join("");
}

function showAdminLogin() {
  const login = byId("adminLogin");
  const change = byId("adminChangePassword");
  const content = byId("adminContent");
  if (login) login.hidden = false;
  if (change) change.hidden = true;
  if (content) content.hidden = true;
}

function showAdminChangePassword() {
  const login = byId("adminLogin");
  const change = byId("adminChangePassword");
  const content = byId("adminContent");
  if (login) login.hidden = true;
  if (change) change.hidden = false;
  if (content) content.hidden = true;
}

function showAdminContent() {
  const login = byId("adminLogin");
  const change = byId("adminChangePassword");
  const content = byId("adminContent");
  if (login) login.hidden = true;
  if (change) change.hidden = true;
  if (content) content.hidden = false;
}

async function loadSession() {
  try {
    const session = await apiFetch("/api/auth/session");
    state.session = session;
    if (!session.authenticated) {
      showAdminLogin();
      return;
    }
    if (session.mustChangePassword) {
      showAdminChangePassword();
      return;
    }
    showAdminContent();
    if (!state.adminLoaded) await loadAdminData();
  } catch (error) {
    showAdminLogin();
  }
}

async function loadAdminData() {
  const [users, tools, settings] = await Promise.all([
    apiFetch("/api/admin/users"),
    apiFetch("/api/admin/tools"),
    apiFetch("/api/admin/settings"),
  ]);

  state.admin.users = Array.isArray(users?.users) ? users.users : [];
  state.admin.tools = Array.isArray(tools?.tools) ? tools.tools : [];
  state.admin.settings = settings || {};
  state.adminLoaded = true;

  renderUsers();
  renderAdminTools();
  renderSettings();
}

async function loadUsers() {
  const data = await apiFetch("/api/admin/users");
  state.admin.users = Array.isArray(data?.users) ? data.users : [];
  renderUsers();
}

function openUserDialog(user = null) {
  const dialog = byId("userDialog");
  if (!dialog) return;

  byId("userFormError").textContent = "";
  byId("userId").value = user?.id ?? "";
  byId("userUid").value = user?.uid ?? "";
  byId("userName").value = user?.name ?? "";
  byId("userActive").checked = user ? user.active !== false : true;
  byId("userDialogTitle").textContent = user ? "Editar usuario" : "Nuevo usuario";

  if (typeof dialog.showModal === "function") {
    dialog.showModal();
  } else {
    dialog.setAttribute("open", "");
  }
}

function closeUserDialog() {
  const dialog = byId("userDialog");
  if (!dialog) return;
  if (typeof dialog.close === "function") {
    dialog.close();
  } else {
    dialog.removeAttribute("open");
  }
}

async function handleUserSubmit(event) {
  event.preventDefault();

  const errorElement = byId("userFormError");
  const id = byId("userId").value;
  const payload = {
    uid: byId("userUid").value.trim().toUpperCase(),
    name: byId("userName").value.trim(),
    active: byId("userActive").checked,
  };

  if (!payload.uid || !payload.name) {
    errorElement.textContent = "UID y nombre son obligatorios.";
    return;
  }

  try {
    if (id) {
      await apiFetch("/api/admin/users", {
        method: "PUT",
        body: { ...payload, id: Number(id) },
      });
      toast("Usuario actualizado", "success");
    } else {
      await apiFetch("/api/admin/users", { method: "POST", body: payload });
      toast("Usuario creado", "success");
    }
    closeUserDialog();
    await loadUsers();
    await refreshStatus();
  } catch (error) {
    errorElement.textContent = error.message;
  }
}

async function deleteUser(id) {
  const user = (state.admin.users || []).find((item) => item.id === id);
  if (!window.confirm(`¿Eliminar la tarjeta de ${user?.name || "este usuario"}?`)) return;

  try {
    await apiFetch(`/api/admin/users?id=${encodeURIComponent(id)}`, { method: "DELETE" });
    toast("Usuario eliminado", "success");
    await loadUsers();
  } catch (error) {
    toast(error.message, "danger");
  }
}

async function saveAdminTool(button) {
  const row = button.closest("tr");
  if (!row) return;

  const id = Number(row.dataset.toolId);
  const name = row.querySelector('[data-field="name"]').value.trim();
  const expectedClass = row.querySelector('[data-field="expectedClass"]').value.trim();
  const active = row.querySelector('[data-field="active"]').checked;

  if (!name) {
    toast("El nombre no puede estar vacío", "danger");
    return;
  }

  button.disabled = true;
  try {
    await apiFetch("/api/admin/tools", {
      method: "PUT",
      body: { id, name, expectedClass, active },
    });
    toast(`Herramienta ${slotForId(id)} guardada`, "success");
    await refreshTools();
  } catch (error) {
    toast(error.message, "danger");
  } finally {
    button.disabled = false;
  }
}

async function saveSettings(event) {
  event.preventDefault();

  const payload = {
    maxEvents: Number(byId("settingMaxEvents").value),
    sessionTimeoutMin: Number(byId("settingSessionTimeout").value),
    cameraEnabled: byId("settingCamera").checked,
    telegramEnabled: byId("settingTelegram").checked,
    telegramToken: byId("settingTelegramToken").value.trim(),
    telegramChatId: byId("settingTelegramChat").value.trim(),
  };

  try {
    const result = await apiFetch("/api/admin/settings", { method: "PUT", body: payload });
    toast(result?.rebootRequired
      ? "Ajustes guardados. Reinicia para aplicar el nuevo límite de eventos."
      : "Ajustes guardados", result?.rebootRequired ? "warning" : "success");
    state.admin.settings = { ...state.admin.settings, ...payload };
    await refreshStatus();
  } catch (error) {
    toast(error.message, "danger");
  }
}

async function forceAnalysis() {
  try {
    await apiFetch("/api/admin/camera/analyze", { method: "POST" });
    toast("Análisis de cámara solicitado", "info");
    window.setTimeout(refreshPhoto, 1800);
    window.setTimeout(refreshStatus, 1200);
  } catch (error) {
    toast(error.message, "danger");
  }
}

async function clearHistory() {
  if (!window.confirm("¿Borrar todo el histórico de eventos? Esta acción no se puede deshacer.")) return;

  try {
    await apiFetch("/api/admin/history/clear", { method: "POST" });
    state.events = [];
    state.eventsTotal = 0;
    state.eventsOffset = 0;
    renderHistory();
    toast("Histórico borrado", "success");
  } catch (error) {
    toast(error.message, "danger");
  }
}

async function restartDevice() {
  if (!window.confirm("¿Reiniciar el armario? La web se desconectará unos segundos.")) return;

  try {
    await apiFetch("/api/admin/restart", { method: "POST" });
    toast("Reiniciando el equipo…", "warning", 8000);
    window.setTimeout(() => window.location.reload(), 9000);
  } catch (error) {
    toast(error.message, "danger");
  }
}

async function resetWifi() {
  if (!window.confirm("¿Borrar las credenciales WiFi y reiniciar? El armario abrirá el portal 'ToolTrack-Setup'.")) return;

  try {
    await apiFetch("/api/admin/wifi/reset", { method: "POST" });
    toast("Credenciales WiFi borradas. Reiniciando…", "warning", 9000);
    window.setTimeout(() => window.location.reload(), 10000);
  } catch (error) {
    toast(error.message, "danger");
  }
}

function notifyEvent(event) {
  const action = ACTION_LABELS[event.action] || event.action;
  const slot = event.slot || "--";

  if (event.action === "taken") {
    toast(`${slot}: ${action} · ${event.user || "sin sesión"}`, "warning");
  } else if (event.action === "returned") {
    toast(`${slot}: ${action} · ${event.user || "sin sesión"}`, "success");
  } else if (event.action === "mismatch") {
    toast(`Discrepancia en ${slot} (${event.confidence || 0}%)`, "danger", 7000);
  } else if (event.action === "access_denied") {
    toast(`Acceso denegado · ${event.user || "tarjeta desconocida"}`, "danger", 7000);
  } else if (event.action === "access_granted") {
    toast(`Acceso autorizado · ${event.user || ""}`, "success");
  }
}

function handleWsMessage(message) {
  if (!message || typeof message !== "object") return;

  switch (message.type) {
    case "snapshot": {
      const data = message.data || {};
      const previous = new Map(state.tools.map((tool) => [tool.id, tool]));
      if (Array.isArray(data.tools)) {
        state.tools = data.tools.map((tool) => ({
          ...(previous.get(tool.id) || {}),
          ...tool,
        }));
        renderCabinet();
        renderToolOptions();
      }

      state.status = {
        ...state.status,
        system: data.system || state.status.system,
        firmware: data.firmware || state.status.firmware,
        uptime: data.uptime ?? state.status.uptime,
        toolsTaken: data.toolsTaken ?? state.status.toolsTaken,
        toolCount: data.toolCount ?? state.status.toolCount,
        cameraOnline: data.cameraOnline,
        camera: {
          ...(state.status.camera || {}),
          online: data.cameraOnline ?? state.status.camera?.online,
        },
        time: {
          ...(state.status.time || {}),
          valid: data.timeValid ?? state.status.time?.valid,
          epoch: data.epoch ?? state.status.time?.epoch,
        },
        sessionUser: data.sessionUser ?? state.status.sessionUser,
      };
      renderStatus();
      break;
    }

    case "tool_changed": {
      const data = message.data || {};
      const index = state.tools.findIndex((tool) => tool.id === data.toolId);
      if (index >= 0) {
        state.tools[index] = { ...state.tools[index], ...data };
        renderCabinet();
      }
      break;
    }

    case "event": {
      const event = message.data || {};
      if (!state.events.some((item) => item.id === event.id)) {
        state.events.unshift(event);
        state.eventsTotal += 1;
      }
      renderHistory();
      notifyEvent(event);
      if (event.action === "mismatch" || event.action === "returned") {
        window.setTimeout(refreshPhoto, 1200);
        window.setTimeout(refreshStatus, 800);
      }
      break;
    }

    case "alert": {
      const data = message.data || {};
      const label = ALERT_LABELS[data.code] || data.code || "Alerta del armario";
      const slot = data.toolId >= 0 ? ` (${slotForId(data.toolId)})` : "";
      toast(`${label}${slot}`, "danger", 8000);
      window.setTimeout(refreshStatus, 600);
      break;
    }

    case "camera_status": {
      const data = message.data || {};
      state.status = {
        ...state.status,
        camera: {
          ...(state.status.camera || {}),
          online: data.online,
          lastSeen: data.lastSeen ?? state.status.camera?.lastSeen,
        },
      };
      renderStatus();
      if (data.online) window.setTimeout(refreshPhoto, 900);
      break;
    }

    case "pong":
      break;

    default:
      break;
  }
}

function connectWebSocket() {
  if (state.ws &&
      (state.ws.readyState === WebSocket.OPEN || state.ws.readyState === WebSocket.CONNECTING)) {
    return;
  }

  if (state.wsRetryTimer) {
    window.clearTimeout(state.wsRetryTimer);
    state.wsRetryTimer = null;
  }

  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(`${protocol}//${window.location.host}/ws`);
  state.ws = socket;

  socket.addEventListener("open", () => {
    markLive(true);
    socket.send(JSON.stringify({ type: "ping" }));
  });

  socket.addEventListener("message", (event) => {
    try {
      handleWsMessage(JSON.parse(event.data));
    } catch {
      // Ignora mensajes que no sean JSON.
    }
  });

  socket.addEventListener("close", () => {
    markLive(false);
    state.ws = null;
    state.wsRetryTimer = window.setTimeout(connectWebSocket, 3000);
  });

  socket.addEventListener("error", () => {
    socket.close();
  });
}

function setupTabs() {
  const tabs = document.querySelectorAll(".tab");
  const panels = document.querySelectorAll(".tab-panel");

  tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      tabs.forEach((item) => item.classList.remove("is-active"));
      panels.forEach((panel) => panel.classList.remove("is-active"));
      tab.classList.add("is-active");

      const target = byId(`tab-${tab.dataset.tab}`);
      if (target) target.classList.add("is-active");

      if (tab.dataset.tab === "history" && !state.historyLoaded) {
        refreshHistory(true);
      }
      if (tab.dataset.tab === "admin") {
        loadSession();
      }
    });
  });
}

function setupSubnav() {
  const buttons = document.querySelectorAll(".subtab");
  const panels = document.querySelectorAll(".admin-panel");

  buttons.forEach((button) => {
    button.addEventListener("click", () => {
      buttons.forEach((item) => item.classList.remove("is-active"));
      panels.forEach((panel) => panel.classList.remove("is-active"));
      button.classList.add("is-active");

      const target = byId(`admin-${button.dataset.admin}`);
      if (target) target.classList.add("is-active");
    });
  });
}

function setupHistory() {
  byId("historyRefreshButton")?.addEventListener("click", () => refreshHistory(true));
  byId("historyMoreButton")?.addEventListener("click", () => refreshHistory(false));
  byId("historySearch")?.addEventListener("input", renderHistory);
  byId("historyTool")?.addEventListener("change", renderHistory);
  byId("refreshButton")?.addEventListener("click", () => {
    refreshStatus();
    refreshTools();
    refreshPhoto();
  });
  byId("refreshPhotoButton")?.addEventListener("click", refreshPhoto);
}

function setupAuth() {
  byId("loginForm")?.addEventListener("submit", async (event) => {
    event.preventDefault();
    const errorElement = byId("loginError");
    const button = byId("loginButton");
    errorElement.textContent = "";
    button.disabled = true;

    try {
      const result = await apiFetch("/api/auth/login", {
        method: "POST",
        body: { password: byId("loginPassword").value },
      });
      byId("loginPassword").value = "";
      state.session = {
        authenticated: true,
        mustChangePassword: Boolean(result?.mustChangePassword),
      };
      state.adminLoaded = false;

      if (result?.mustChangePassword) {
        showAdminChangePassword();
        toast("Cambia la contraseña inicial antes de continuar", "warning", 7000);
      } else {
        showAdminContent();
        await loadAdminData();
      }
    } catch (error) {
      errorElement.textContent = error.message;
    } finally {
      button.disabled = false;
    }
  });

  byId("changePasswordForm")?.addEventListener("submit", async (event) => {
    event.preventDefault();
    const errorElement = byId("changePasswordError");
    const current = byId("currentPassword").value;
    const next = byId("newPassword").value;
    const confirmation = byId("confirmPassword").value;
    errorElement.textContent = "";

    if (next !== confirmation) {
      errorElement.textContent = "Las contraseñas nuevas no coinciden.";
      return;
    }

    try {
      await apiFetch("/api/auth/change-password", {
        method: "POST",
        body: { current, new: next },
      });
      byId("currentPassword").value = "";
      byId("newPassword").value = "";
      byId("confirmPassword").value = "";
      state.session.mustChangePassword = false;
      state.adminLoaded = false;
      showAdminContent();
      await loadAdminData();
      toast("Contraseña actualizada", "success");
    } catch (error) {
      errorElement.textContent = error.message;
    }
  });

  byId("logoutButton")?.addEventListener("click", async () => {
    try {
      await apiFetch("/api/auth/logout", { method: "POST" });
    } catch {
      // La sesión local se cierra igualmente.
    }
    state.session = { authenticated: false, mustChangePassword: false };
    state.adminLoaded = false;
    showAdminLogin();
    toast("Sesión cerrada", "info");
  });
}

function setupAdmin() {
  byId("addUserButton")?.addEventListener("click", () => openUserDialog());
  byId("cancelUserButton")?.addEventListener("click", closeUserDialog);
  byId("userForm")?.addEventListener("submit", handleUserSubmit);

  byId("usersBody")?.addEventListener("click", (event) => {
    const editButton = event.target.closest("[data-user-edit]");
    if (editButton) {
      const id = Number(editButton.dataset.userEdit);
      const user = (state.admin.users || []).find((item) => item.id === id);
      if (user) openUserDialog(user);
      return;
    }

    const deleteButton = event.target.closest("[data-user-delete]");
    if (deleteButton) {
      deleteUser(Number(deleteButton.dataset.userDelete));
    }
  });

  byId("toolsBody")?.addEventListener("click", (event) => {
    const saveButton = event.target.closest("[data-tool-save]");
    if (saveButton) saveAdminTool(saveButton);
  });

  byId("settingsForm")?.addEventListener("submit", saveSettings);
}

function setupSystem() {
  byId("forceAnalysisButton")?.addEventListener("click", forceAnalysis);
  byId("clearHistoryButton")?.addEventListener("click", clearHistory);
  byId("wifiResetButton")?.addEventListener("click", resetWifi);
  byId("restartButton")?.addEventListener("click", restartDevice);
}

async function refreshAll() {
  await Promise.allSettled([
    refreshStatus(),
    refreshTools(),
    refreshPhoto(),
    refreshHistory(true),
  ]);
}

function init() {
  setupTabs();
  setupSubnav();
  setupHistory();
  setupAuth();
  setupAdmin();
  setupSystem();

  renderClock();
  renderCabinet();
  refreshAll();
  connectWebSocket();

  window.setInterval(renderClock, 1000);
  window.setInterval(refreshStatus, 10000);
  window.setInterval(refreshTools, 15000);
  window.setInterval(() => {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
      state.ws.send(JSON.stringify({ type: "ping" }));
    }
  }, 25000);
}

document.addEventListener("DOMContentLoaded", init);

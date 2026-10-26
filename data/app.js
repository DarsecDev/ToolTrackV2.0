/**
 * ToolTrack — interfaz web
 * Fase 0: estructura visual y datos de ejemplo.
 * Fase 1: se conectará a /api/status, /api/tools y /api/events.
 */

const TOOL_COUNT = 12;
const GRID_COLS = 3;

const state = {
  tools: Array.from({ length: TOOL_COUNT }, (_, index) => ({
    id: index,
    name: `Herramienta ${index + 1}`,
    present: null,
    owner: null,
  })),
};

function slotCode(index) {
  const row = Math.floor(index / GRID_COLS);
  const col = (index % GRID_COLS) + 1;
  return `${String.fromCharCode(65 + row)}${col}`;
}

function renderCabinet() {
  const grid = document.getElementById("cabinetGrid");
  if (!grid) return;

  grid.innerHTML = "";
  state.tools.forEach((tool) => {
    const code = slotCode(tool.id);
    const presentClass =
      tool.present === true ? "is-present" : tool.present === false ? "is-missing" : "";
    const dotClass =
      tool.present === true
        ? "dot-present"
        : tool.present === false
          ? "dot-missing"
          : "dot-unknown";
    const stateText =
      tool.present === true ? "Presente" : tool.present === false ? "Fuera" : "Sin datos";

    const card = document.createElement("article");
    card.className = `slot ${presentClass}`;
    card.innerHTML = `
      <span class="slot-code">${code}</span>
      <span class="slot-name">${tool.name}</span>
      <span class="slot-state"><i class="dot ${dotClass}"></i>${stateText}</span>
    `;
    grid.appendChild(card);
  });
}

function setStatus(id, text) {
  const element = document.getElementById(id);
  if (element) element.textContent = text;
}

function updateClock() {
  const now = new Date();
  setStatus("clock", now.toLocaleTimeString("es-MX", { hour12: false }));
}

function setupTabs() {
  const tabs = document.querySelectorAll(".tab");
  const panels = document.querySelectorAll(".tab-panel");

  tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      tabs.forEach((item) => item.classList.remove("is-active"));
      panels.forEach((panel) => panel.classList.remove("is-active"));
      tab.classList.add("is-active");
      const target = document.getElementById(`tab-${tab.dataset.tab}`);
      if (target) target.classList.add("is-active");
    });
  });
}

async function refreshStatus() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) throw new Error("API no disponible");
    const data = await response.json();

    setStatus("statusSystem", data.system || "Activo");
    setStatus("statusWifi", data.wifi || "Sin datos");
    setStatus("statusCamera", data.camera || "Sin datos");
    setStatus(
      "statusTaken",
      `${data.toolsTaken ?? 0} / ${data.toolCount ?? TOOL_COUNT}`,
    );

    if (Array.isArray(data.tools)) {
      state.tools = data.tools.map((tool, index) => ({
        id: tool.id ?? index,
        name: tool.name || `Herramienta ${index + 1}`,
        present: typeof tool.present === "boolean" ? tool.present : null,
        owner: tool.owner || null,
      }));
      renderCabinet();
    }
  } catch (error) {
    setStatus("statusSystem", "Fase 0 · API pendiente");
    setStatus("statusWifi", "Sin datos");
    setStatus("statusCamera", "Sin datos");
  }
}

function init() {
  setupTabs();
  renderCabinet();
  updateClock();
  setInterval(updateClock, 1000);
  refreshStatus();

  const refreshButton = document.getElementById("refreshButton");
  if (refreshButton) refreshButton.addEventListener("click", refreshStatus);
}

document.addEventListener("DOMContentLoaded", init);

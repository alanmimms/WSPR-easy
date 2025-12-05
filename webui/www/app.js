// WSPR-ease Web UI Application

const API_BASE = '/api';
const APP_VERSION = 'dev';  // Replaced by Makefile during build
let currentConfig = null;
let originalConfig = null;  // For cancel/dirty detection
let lastStatusTime = 0;
const OFFLINE_TIMEOUT_MS = 5000;
let isDirty = false;

// Check server version and reload if mismatched
async function checkVersion() {
  try {
    const response = await fetch(`${API_BASE}/version`);
    if (!response.ok) return;

    const data = await response.json();
    if (data.version && data.version !== APP_VERSION) {
      console.log(`Version mismatch: app=${APP_VERSION} server=${data.version}, reloading...`);
      location.reload(true);
    }
  } catch (error) {
    // Ignore version check errors
  }
}

// Update server online/offline status
function updateServerStatus(online) {
  const el = document.getElementById('server-status');
  if (online) {
    el.textContent = 'ONLINE';
    el.className = 'value status-online';
  } else {
    el.textContent = 'OFFLINE';
    el.className = 'value status-offline';
  }
}

// Update dirty state and button enables
function setDirty(dirty) {
  isDirty = dirty;
  document.getElementById('save-all').disabled = !dirty;
  document.getElementById('cancel-changes').disabled = !dirty;
}

// Mark as dirty when any config input changes
function markDirty() {
  setDirty(true);
}

// Tab switching
document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    const tabName = tab.dataset.tab;

    // Update tab buttons
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');

    // Update tab content
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.getElementById(`${tabName}-tab`).classList.add('active');
  });
});

// Load configuration
async function loadConfig() {
  try {
    const response = await fetch(`${API_BASE}/config`);
    if (!response.ok) throw new Error('Failed to load config');

    currentConfig = await response.json();
    originalConfig = JSON.parse(JSON.stringify(currentConfig));  // Deep copy
    updateConfigUI(currentConfig);
    setDirty(false);
  } catch (error) {
    console.error('Error loading config:', error);
    alert('Failed to load configuration');
  }
}

// Update UI with config
function updateConfigUI(config) {
  document.getElementById('callsign').value = config.callsign;
  document.getElementById('grid').value = config.gridSquare;
  document.getElementById('power').value = config.powerDbm;
  document.getElementById('mode').value = config.mode;
  document.getElementById('interval').value = config.slotIntervalMin;
  document.getElementById('band-list-input').value = config.bandList || '';

  // Show/hide band list input based on mode
  updateBandListVisibility();

  // Update bands checkboxes
  const bandsList = document.getElementById('bands-list');
  bandsList.innerHTML = '';

  config.bands.forEach((band, index) => {
    const bandItem = document.createElement('div');
    bandItem.className = 'band-item';
    bandItem.innerHTML = `
      <input type="checkbox" id="band-${index}" ${band.enabled ? 'checked' : ''}>
      <label for="band-${index}">${band.name || `Band ${index}`} (${(band.freqHz / 1000000).toFixed(3)} MHz)</label>
    `;
    bandsList.appendChild(bandItem);

    // Mark dirty when band checkbox changes
    document.getElementById(`band-${index}`).addEventListener('change', markDirty);
  });

  // Update time windows
  updateTimeWindowsUI(config.bands);
}

// Show/hide band list input based on mode selection
function updateBandListVisibility() {
  const mode = document.getElementById('mode').value;
  const bandListGroup = document.getElementById('band-list-group');
  bandListGroup.style.display = mode === 'list' ? 'block' : 'none';
}

document.getElementById('mode').addEventListener('change', () => {
  updateBandListVisibility();
  markDirty();
});

// Mark dirty on config field changes
['callsign', 'grid', 'power', 'interval', 'band-list-input'].forEach(id => {
  document.getElementById(id).addEventListener('change', markDirty);
  document.getElementById(id).addEventListener('input', markDirty);
});

// Update time windows UI
function updateTimeWindowsUI(bands) {
  const container = document.getElementById('time-windows-list');
  container.innerHTML = '';

  bands.forEach((band, index) => {
    const tw = band.timeWindow || {};
    const item = document.createElement('div');
    item.className = 'time-window-item';
    item.innerHTML = `
      <div class="band-name">${band.name} (${(band.freqHz / 1000000).toFixed(3)} MHz)</div>
      <label>
        <input type="checkbox" id="tw-enabled-${index}" ${tw.enabled ? 'checked' : ''}>
        Enable time window
      </label>
      <div class="time-window-row" id="tw-row-${index}" style="display: ${tw.enabled ? 'flex' : 'none'};">
        <label>From:</label>
        <select id="tw-start-base-${index}">
          <option value="utc" ${tw.startBase === 'utc' ? 'selected' : ''}>UTC</option>
          <option value="local" ${tw.startBase === 'local' ? 'selected' : ''}>Local</option>
          <option value="sunrise" ${tw.startBase === 'sunrise' ? 'selected' : ''}>Sunrise</option>
          <option value="sunset" ${tw.startBase === 'sunset' ? 'selected' : ''}>Sunset</option>
        </select>
        <input type="number" id="tw-start-offset-${index}" value="${tw.startOffsetMin || 0}" style="width:70px;">
        <span>min</span>
        <label>To:</label>
        <select id="tw-end-base-${index}">
          <option value="utc" ${tw.endBase === 'utc' ? 'selected' : ''}>UTC</option>
          <option value="local" ${tw.endBase === 'local' ? 'selected' : ''}>Local</option>
          <option value="sunrise" ${tw.endBase === 'sunrise' ? 'selected' : ''}>Sunrise</option>
          <option value="sunset" ${tw.endBase === 'sunset' ? 'selected' : ''}>Sunset</option>
        </select>
        <input type="number" id="tw-end-offset-${index}" value="${tw.endOffsetMin || 1440}" style="width:70px;">
        <span>min</span>
      </div>
    `;
    container.appendChild(item);

    // Toggle time window row visibility and mark dirty
    document.getElementById(`tw-enabled-${index}`).addEventListener('change', (e) => {
      document.getElementById(`tw-row-${index}`).style.display = e.target.checked ? 'flex' : 'none';
      markDirty();
    });

    // Mark dirty on time window changes
    [`tw-start-base-${index}`, `tw-start-offset-${index}`, `tw-end-base-${index}`, `tw-end-offset-${index}`].forEach(id => {
      document.getElementById(id).addEventListener('change', markDirty);
      document.getElementById(id).addEventListener('input', markDirty);
    });
  });
}

// Gather config from UI
function gatherConfigFromUI() {
  const config = {
    ...currentConfig,
    callsign: document.getElementById('callsign').value,
    gridSquare: document.getElementById('grid').value,
    powerDbm: parseInt(document.getElementById('power').value),
    mode: document.getElementById('mode').value,
    bandList: document.getElementById('band-list-input').value,
    slotIntervalMin: parseInt(document.getElementById('interval').value),
  };

  // Update band enables and time windows
  config.bands.forEach((band, index) => {
    const checkbox = document.getElementById(`band-${index}`);
    if (checkbox) {
      band.enabled = checkbox.checked;
    }

    // Update time window
    const twEnabled = document.getElementById(`tw-enabled-${index}`);
    if (twEnabled) {
      band.timeWindow = {
        enabled: twEnabled.checked,
        startBase: document.getElementById(`tw-start-base-${index}`).value,
        startOffsetMin: parseInt(document.getElementById(`tw-start-offset-${index}`).value) || 0,
        endBase: document.getElementById(`tw-end-base-${index}`).value,
        endOffsetMin: parseInt(document.getElementById(`tw-end-offset-${index}`).value) || 1440,
      };
    }
  });

  return config;
}

// Save configuration
document.getElementById('save-all').addEventListener('click', async () => {
  try {
    const config = gatherConfigFromUI();

    const response = await fetch(`${API_BASE}/config`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config)
    });

    if (!response.ok) throw new Error('Failed to save config');

    await loadConfig();  // Reload and reset dirty state
  } catch (error) {
    console.error('Error saving config:', error);
    alert('Failed to save configuration');
  }
});

// Cancel changes - restore from originalConfig
document.getElementById('cancel-changes').addEventListener('click', () => {
  if (!originalConfig) return;
  currentConfig = JSON.parse(JSON.stringify(originalConfig));
  updateConfigUI(currentConfig);
  setDirty(false);
});

// Trigger transmission
document.getElementById('trigger-tx').addEventListener('click', async () => {
  try {
    const response = await fetch(`${API_BASE}/tx/trigger`, { method: 'POST' });
    if (!response.ok) throw new Error('Failed to trigger TX');

    const result = await response.json();
    alert(result.message || 'Transmission triggered');
  } catch (error) {
    console.error('Error triggering TX:', error);
    alert('Failed to trigger transmission');
  }
});

// Load status
async function loadStatus() {
  try {
    const response = await fetch(`${API_BASE}/status`);
    if (!response.ok) throw new Error('Failed to load status');

    const status = await response.json();
    lastStatusTime = Date.now();
    updateServerStatus(true);

    // Update status bar
    document.getElementById('tx-status').textContent = status.tx.active ? 'Transmitting' : 'Idle';
    document.getElementById('gnss-status').textContent = status.gnss.locked ?
      `Locked (${status.gnss.satellites} sats)` : 'No Lock';
    document.getElementById('clock-status').textContent = status.clock.source.toUpperCase();

    if (status.tx.nextTxSec) {
      const minutes = Math.floor(status.tx.nextTxSec / 60);
      const seconds = status.tx.nextTxSec % 60;
      document.getElementById('next-tx').textContent = `${minutes}:${seconds.toString().padStart(2, '0')}`;
    }

    // Update status JSON view
    document.getElementById('status-json').textContent = JSON.stringify(status, null, 2);
  } catch (error) {
    console.error('Error loading status:', error);
    updateServerStatus(false);
  }
}

// Load file list
async function loadFiles() {
  try {
    const response = await fetch(`${API_BASE}/files?path=/`);
    if (!response.ok) throw new Error('Failed to load files');

    const data = await response.json();
    const fileList = document.getElementById('file-list');
    fileList.innerHTML = '';

    // Filter out directories - flat file view only
    const files = data.files.filter(f => !f.isDirectory);

    if (files.length === 0) {
      fileList.innerHTML = '<p style="color: var(--secondary); padding: 10px;">No files</p>';
      return;
    }

    files.forEach(file => {
      const fileItem = document.createElement('div');
      fileItem.className = 'file-item';
      fileItem.innerHTML = `
        <div class="file-info">
          <span class="file-name">${file.name}</span>
          <span class="file-size">${formatBytes(file.size)}</span>
        </div>
        <div class="file-actions">
          <button class="btn btn-secondary" onclick="downloadFile('${file.name}')">Download</button>
          <button class="btn btn-danger" onclick="deleteFile('${file.name}')">Delete</button>
        </div>
      `;
      fileList.appendChild(fileItem);
    });
  } catch (error) {
    console.error('Error loading files:', error);
  }
}

// Download file
async function downloadFile(filename) {
  try {
    const response = await fetch(`${API_BASE}/files/${filename}`);
    if (!response.ok) throw new Error('Failed to download file');

    const blob = await response.blob();
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    window.URL.revokeObjectURL(url);
    document.body.removeChild(a);
  } catch (error) {
    console.error('Error downloading file:', error);
    alert('Failed to download file');
  }
}

// Delete file
async function deleteFile(filename) {
  if (!confirm(`Delete "${filename}"?`)) return;

  try {
    const response = await fetch(`${API_BASE}/files/${filename}`, { method: 'DELETE' });
    if (!response.ok) throw new Error('Failed to delete file');

    loadFiles();
  } catch (error) {
    console.error('Error deleting file:', error);
    alert('Failed to delete file');
  }
}

// Upload file
document.getElementById('upload-btn').addEventListener('click', () => {
  document.getElementById('file-input').click();
});

document.getElementById('file-input').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) return;

  try {
    const content = await file.text();
    const response = await fetch(`${API_BASE}/files/${file.name}`, {
      method: 'PUT',
      body: content
    });

    if (!response.ok) throw new Error('Failed to upload file');

    loadFiles();
    e.target.value = '';  // Reset input
  } catch (error) {
    console.error('Error uploading file:', error);
    alert('Failed to upload file');
  }
});

document.getElementById('refresh-files').addEventListener('click', loadFiles);

// Format bytes to human readable
function formatBytes(bytes) {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}

// Initialize
checkVersion();  // Check version on load
loadConfig();
loadStatus();
loadFiles();

// Auto-refresh status every 2 seconds, check version every 10 seconds
setInterval(loadStatus, 2000);
setInterval(checkVersion, 10000);

// WebSocket configuration
const gateway = `ws://${window.location.hostname}/ws`;
let websocket;

// Initialize the interface when the page loads
async function initializeInterface() {
    console.log("Page loaded, initializing...");
    initWebSocket();
    await sendTimeToESP32();
    await syncButtonStates();
    await updateFilesList(); // Ajout de la mise à jour des fichiers
}

// File management functions
async function updateFilesList() {
    try {
        const response = await fetch('/list-files');
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const files = await response.json();
        displayFiles(files);
    } catch (error) {
        console.error("Error fetching files list:", error);
    }
}

function displayFiles(files) {
    const filesContainer = document.getElementById('files-list');
    if (!filesContainer) return;

    filesContainer.innerHTML = '';

    if (files.length === 0) {
        filesContainer.innerHTML = '<div class="empty-files">Aucun fichier disponible</div>';
        return;
    }

    files.forEach(file => {
        const fileDiv = document.createElement('div');
        fileDiv.className = 'file-item';
        fileDiv.innerHTML = `
            <span class="file-name">${file}</span>
            <div class="file-actions">
                <button class="file-action-btn download-btn" title="Télécharger" onclick="downloadFile('${file}')">💾</button>
                <button class="file-action-btn delete-btn" title="Supprimer" onclick="deleteFile('${file}')">🗑️</button>
            </div>
        `;
        filesContainer.appendChild(fileDiv);
    });
}

async function downloadFile(filename) {
    try {
        const response = await fetch(`/download?file=${encodeURIComponent(filename)}`);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const blob = await response.blob();
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        window.URL.revokeObjectURL(url);
    } catch (error) {
        console.error("Error downloading file:", error);
        alert("Erreur lors du téléchargement du fichier");
    }
}

async function deleteFile(filename) {
    if (!confirm(`Voulez-vous vraiment supprimer le fichier ${filename} ?`)) {
        return;
    }

    try {
        const response = await fetch(`/delete?file=${encodeURIComponent(filename)}`, {
            method: 'DELETE'
        });
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        await updateFilesList();
    } catch (error) {
        console.error("Error deleting file:", error);
        alert("Erreur lors de la suppression du fichier");
    }
}

// Add page load event listener
window.addEventListener('load', initializeInterface);

// WebSocket functions
function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    getReadings();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function getReadings() {
    websocket.send("getReadings");
}

// Message handler
function onMessage(event) {
    console.log("WebSocket received:", event.data);
    const data = JSON.parse(event.data);

    // Ne pas mettre à jour l'intervalle via WebSocket si l'utilisateur est en train de modifier le sélecteur
    const intervalSelect = document.getElementById("measure-interval");
    if (intervalSelect && !intervalSelect.matches(':focus') && data.measureInterval !== undefined) {
        const newValue = data.measureInterval.toString();
        if (intervalSelect.value !== newValue) {
            console.log("Updating interval selector to:", newValue);
            intervalSelect.value = newValue;
        }
    }

    for (const [key, value] of Object.entries(data)) {
        const element = document.getElementById(key);
        if (!element) {
            console.warn(`No element with id "${key}" found in DOM.`);
            continue;
        }

        // Special handling for GPS fix
        if (key === "gpsfix") {
            if (value === "1") {
                element.classList.add("connected");
                element.title = "GPS connecté";
            } else {
                element.classList.remove("connected");
                element.title = "GPS non connecté";
            }
            continue;
        }

        // Special handling for timestamp
        if (key === "time_utc") {
            const date = new Date(value);
            element.innerHTML = date.toLocaleString();
            element.title = "Dernière mise à jour";
            continue;
        }

        // Default handling
        element.innerHTML = value;
    }
}

// Time synchronization
async function sendTimeToESP32() {
    const now = new Date();
    const timeData = {
        year: now.getFullYear(),
        month: now.getMonth() + 1,
        day: now.getDate(),
        hour: now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds(),
        timezoneOffset: now.getTimezoneOffset()
    };

    console.log("Sending time to ESP32:", timeData);
    try {
        const response = await fetch('/set-time', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(timeData)
        });
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
    } catch (error) {
        console.error("Error sending time to ESP32:", error);
    }
}

// Button state management
async function syncButtonStates() {
    console.log("Synchronizing button states...");
    try {
        const response = await fetch('/status');
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const status = await response.json();

        // Start/Stop Measure button
        const startStopButton = document.getElementById("startstopmeas-btn");
        if (startStopButton) {
            if (status.measuring) {
                startStopButton.innerHTML = "🛑 Stop Measure";
                startStopButton.style.backgroundColor = "red";
                startStopButton.dataset.state = "false";
            } else {
                startStopButton.innerHTML = "▶️ Start Measure";
                startStopButton.style.backgroundColor = "green";
                startStopButton.dataset.state = "true";
            }
        }

        // Measurement interval
        if (status.measureInterval !== undefined) {
            const intervalSelect = document.getElementById("measure-interval");
            if (intervalSelect && !intervalSelect.matches(':focus')) {
                intervalSelect.value = status.measureInterval.toString();
            }
        }
    } catch (error) {
        console.error("Error fetching status:", error);
    }
}

// Measurement control
document.getElementById("startstopmeas-btn").addEventListener("click", function () {
    const button = this;
    fetch("/startstopmeas")
        .then(response => {
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            if (button.innerHTML === "▶️ Start Measure") {
                button.innerHTML = "🛑 Stop Measure";
                button.style.backgroundColor = "red";
                button.dataset.state = "false";
            } else {
                button.innerHTML = "▶️ Start Measure";
                button.style.backgroundColor = "green";
                button.dataset.state = "true";
            }
        })
        .catch(error => {
            console.error("Error toggling measurement:", error);
        });
});

// Interval management - Modified version
const intervalSelect = document.getElementById("measure-interval");
if (intervalSelect) {
    // Vérifier l'intervalle au démarrage
    fetch('/get-interval')
        .then(response => response.json())
        .then(data => {
            if (data.interval !== undefined) {
                intervalSelect.value = data.interval.toString();
            }
        })
        .catch(error => console.error("Error fetching initial interval:", error));

    // Gestionnaire d'événements avec XMLHttpRequest
    intervalSelect.addEventListener('change', function () {
        const newInterval = this.value;
        const xhr = new XMLHttpRequest();

        xhr.open('POST', '/set-interval', true);
        xhr.setRequestHeader('Content-Type', 'application/json');

        xhr.onreadystatechange = function () {
            if (xhr.readyState === 4) {
                if (xhr.status === 200) {
                    try {
                        const response = JSON.parse(xhr.responseText);
                        if (response.currentInterval !== undefined) {
                            intervalSelect.value = response.currentInterval.toString();
                        }
                    } catch (e) {
                        console.error("Error parsing response:", e);
                    }
                }
            }
        };

        const data = JSON.stringify({ interval: parseInt(newInterval) });
        xhr.send(data);
    });
}
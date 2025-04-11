
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
    syncButtonStates(); // Nouvelle fonction pour mettre à jour l’état des boutons
}

function getReadings() {
    websocket.send("getReadings");
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

// When websocket is established, call the getReadings() function
function onOpen(event) {
    console.log('Connection opened');
    getReadings();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

// Function that receives the message from the ESP32 with the readings
function onMessage(event) {
    console.log(event.data);
    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);

    for (var i = 0; i < keys.length; i++) {
        var key = keys[i];
        document.getElementById(key).innerHTML = myObj[key];
    }
}

function toggleCheckbox(x) {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/" + x, true);
    xhr.send();
function syncButtonStates() {
    console.log("Synchronizing button states...");
    fetch('/status')
        .then(response => response.json())
        .then(status => {
            // Start/Stop Measure
            const startStopButton = document.getElementById("startstopmeas-btn");
            if (status.measuring) {
                startStopButton.innerHTML = "🛑 Stop Measure";
                startStopButton.style.backgroundColor = "red";
                startStopButton.dataset.state = "false";
            } else {
                startStopButton.innerHTML = "▶️ Start Measure";
                startStopButton.style.backgroundColor = "green";
                startStopButton.dataset.state = "true";
            }

            // Mode
            const modeButton = document.getElementById("mode-btn");
            if (status.mode) {
                modeButton.innerHTML = "🔁 Periodic";
                modeButton.style.backgroundColor = "greenyellow";
                modeButton.dataset.state = "true";
            } else {
                modeButton.innerHTML = "▶️ Continuous";
                modeButton.style.backgroundColor = "darkviolet";
                modeButton.dataset.state = "false";
            }
        })
        .catch(error => {
            console.error("Erreur lors de la récupération de l'état :", error);
        });
}
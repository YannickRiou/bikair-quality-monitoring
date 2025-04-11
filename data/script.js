
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
    sendTimeToESP32();
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
document.getElementById("startstopmeas-btn").addEventListener("click", function () {
    var btn = this;
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/startstopmeas", true);
    xhr.send();
    var startStopButton = document.getElementById("startstopmeas-btn");
    if (startStopButton.innerHTML === "▶️ Start Measure") {
        startStopButton.innerHTML = "🛑 Stop Measure";
        startStopButton.style.backgroundColor = "red";
        btn.dataset.state = "false";
    } else {
        startStopButton.innerHTML = "▶️ Start Measure";
        startStopButton.style.backgroundColor = "green";
        btn.dataset.state = "true";
    }
});
document.getElementById("mode-btn").addEventListener("click", function () {
    console.log("modeToggle button clicked");
    var btn = this;
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/mode", true);
    xhr.send();
    var modeToggleButton = document.getElementById("mode-btn");
    if (modeToggleButton.innerHTML === "🔁 Periodic") {
        modeToggleButton.innerHTML = "▶️ Continuous";
        modeToggleButton.style.backgroundColor = "darkviolet";
        btn.dataset.state = "false";
    } else {
        modeToggleButton.innerHTML = "🔁 Periodic";
        modeToggleButton.style.backgroundColor = "greenyellow";
        btn.dataset.state = "true";
    }
});

document.getElementById("sleep-btn").addEventListener("click", function () {
    var btn = this;
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/sleep", true);
    xhr.send();
    var sleepButton = document.getElementById("sleep-btn");
    if (sleepButton.innerHTML === "💤 Sleep") {
        sleepButton.innerHTML = "💤 Sleeping";
        sleepButton.style.backgroundColor = "red";
        btn.dataset.state = "false";
    }
});

async function sendTimeToESP32() {
    const now = new Date();

    const timeData = {
        year: now.getFullYear(),
        month: now.getMonth() + 1, // JS months are 0-indexed
        day: now.getDate(),
        hour: now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds(),
        timezoneOffset: now.getTimezoneOffset() // en minutes
    };

    // console log the time for debug 
    console.log("Sending time to ESP32:", timeData);

    await fetch('/set-time', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(timeData)
    });
}

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
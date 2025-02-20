
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
// Init web socket when the page loads
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
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
        if (key === "gpsfix") {
            var gpsElem = document.getElementById("gpsfix");
            // Remove both fix and nofix classes to start fresh
            gpsElem.classList.remove("fix", "nofix", "goodfix");
            if (myObj[key].trim() === "0") {
                gpsElem.classList.add("nofix");
            } else if (myObj[key].trim() === "1") {
                gpsElem.classList.add("fix");
            } else if (myObj[key].trim() === "2") {
                gpsElem.classList.add("goodfix");
            }
        } else if (key === "aqi") {
            var aqiElem = document.getElementById("aqi");
            aqiElem.classList.remove("excellent", "good", "moderate", "poor", "unhealthy", "stale");
            if (myObj[key].trim() === "1") {
                aqiElem.classList.add("excellent");
            } else if (myObj[key].trim() === "2") {
                aqiElem.classList.add("good");
            } else if (myObj[key].trim() === "3") {
                aqiElem.classList.add("moderate");
            } else if (myObj[key].trim() === "4") {
                aqiElem.classList.add("poor");
            } else if (myObj[key].trim() === "5") {
                aqiElem.classList.add("unhealthy");
            } else {
                aqiElem.classList.add("stale");
            }
        } else {
            // For other keys, update the innerHTML of the corresponding element
            var elem = document.getElementById(key);
            if (elem) {
                elem.innerHTML = myObj[key];
            }
        }
    }
}

function toggleCheckbox(x) {
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/" + x, true);
    xhr.send();
}
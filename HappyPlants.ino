#include <Arduino.h>
#include <dht.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <stdio.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <time.h>

dht DHT, DHT_OLD;
int pumpRelay = 18;
int pumpstate = 0;
int lightRelay = 19;
int lightstate = 0;

#define WATERLEVEL 2

struct {
	long startTime_l;
	char startTime_s[6];
	long duration_l;
	char duration_s[6];
} lightControl;

struct {
	long interval;
	long duration;
} pumpControl;


int signal = 0;
float temperature[100];

#define DHT22_PIN 5

const char *ssid = "ssid";
const char *password = "pass";

WebServer server(80);

const int led = 13;

#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00"

#define STRINGIFY(...) #__VA_ARGS__

char cssPart[4096];
char jsPart[4096];

void setup() {

	pinMode(led, OUTPUT);
	digitalWrite(led, 0);
	Serial.begin(115200);

	delay(500);

	if(SPIFFS.begin(true)) {
		Serial.println("SPIFFS mounted");
	} else {
		Serial.println("error mount SPIFFS");
	}

	//SPIFFS.format();

    // Get all information of SPIFFS
	unsigned int total = SPIFFS.totalBytes();
	unsigned int used = SPIFFS.usedBytes();

	Serial.println("===== File system info =====");
	Serial.printf("total space: %d byte; used space: %d bytes\n", total, used);

	if(SPIFFS.exists("/lightctrl.dat")) {
		File f = SPIFFS.open("/lightctrl.dat", FILE_READ);
		f.read((byte *)&lightControl, sizeof(lightControl));
		f.close();
		Serial.printf("start light: %s\nlight duration: %s\n", lightControl.startTime_s, lightControl.duration_s);
	} else {
		lightControl.startTime_l = 3600 * 6;
		lightControl.duration_l = 3600 * 18;
		updateLightControl();
	}

	if(SPIFFS.exists("/pumpctrl.dat")) {
		File f = SPIFFS.open("/pumpctrl.dat", FILE_READ);
		f.read((byte *)&pumpControl, sizeof(pumpControl));
		f.close();
		Serial.printf("pump interval: %d\npump duration: %d\n", pumpControl.interval, pumpControl.duration);
	} else {
		pumpControl.interval = 15;
		pumpControl.duration = 5;
		updatePumpControl();
	}

	strcpy(cssPart, STRINGIFY(
body {
	background-color: #222;
	font-family: Courier, Fixed;
	Color: aquamarine;
	font-size: large;
}
span.right {
	float: right;
	margin-top: -0px;
}
.time {
	background-color: #222;
	color: aquamarine;
	border-color: #444;
	border-style: solid;
}
span#header {
	font-size: larger;
}
div#header {
	border-bottom-style: solid;
	border-bottom-width: 2px;
	border-bottom-color: #444;
}
.chart {
}
u {
	color: #f80;
	font-weight: bold;
	text-decoration: none;
}
label {
	border-style: solid;
	border-bottom-width: 1px;
	border-color: #444;
}
	));


	snprintf(jsPart, 4096, STRINGIFY(
		document.addEventListener("DOMContentLoaded", function(e) {

		var starttime = document.getElementById('starttime');
		starttime.addEventListener('focusout',
			function(e) {
				ajaxPost('/lightRelay', {
					"startTime": this.valueAsNumber
				},
				function(data) {
					console.log(data);
				});
			}
		);

		var duration = document.getElementById('duration');
		duration.addEventListener('focusout',
			function(e) {
				ajaxPost('/lightRelay', {
					"duration": this.valueAsNumber
				},
				function(data) {
					console.log(data);
				});
			}
		);

		function pumpInterval(event) {
			if(event.type) {
				ajaxPost('/pumpRelay', {
					"interval": this.value
				}, function(data) {
					console.log(data);
				});
			}
		}
		function pumpDuration(event) {
			if(event.type) {
				ajaxPost('/pumpRelay', {
					"duration": this.value
				}, function(data) {
					console.log(data);
				});
			}
		}
		document.querySelectorAll("input[name='interval']").forEach((input) => {
			if(input.value == %d) {
				input.checked = true;
			}
			input.addEventListener('change', pumpInterval);
		});
		document.querySelectorAll("input[name='duration']").forEach((input) => {
			if(input.value == %d) {
				input.checked = true;
			}
			input.addEventListener('change', pumpDuration);
		});

		function ajaxCall(url, callback) {
			var ajax;
			ajax = new XMLHttpRequest();
			ajax.onreadystatechange = function() {
				if(ajax.readyState == 4 && ajax.status == 200) {
					callback(ajax.responseText);
       			}
   			};
			ajax.open("GET", url, true);
   			ajax.send();
		}

		function ajaxPost(url, data, success) {
			var params = typeof data == 'string'
			? data
			: Object.keys(data).map(
				function(k) {
					return(encodeURIComponent(k) + '=' + encodeURIComponent(data[k]))
				}
			).join('&');

			var xhr = window.XMLHttpRequest
			? new XMLHttpRequest()
			: new ActiveXObject("Microsoft.XMLHTTP");

			xhr.open('POST', url);
			xhr.onreadystatechange = function() {
				if (xhr.readyState>3 && xhr.status==200) {
					success(xhr.responseText);
				}
			};
			xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');
			xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
			xhr.send(params);
			return(xhr);
		}

		window.onload = function(e){
			var smoothie = new SmoothieChart({
			    millisPerPixel: 2000,
   				grid: {fillStyle: '#222', strokeStyle: '#444',millisPerLine:80000,verticalSections:5},
			});
			var smoothie2 = new SmoothieChart({
				millisPerPixel: 2000,
   				grid: {fillStyle: '#222', strokeStyle: '#444',millisPerLine:80000,verticalSections:5},
			});
			smoothie.streamTo(document.getElementById("mycanvas"));
			smoothie2.streamTo(document.getElementById("mycanvas2"));

			var line1 = new TimeSeries();
			var line2 = new TimeSeries();
			setInterval(function() {
				ajaxCall("/combined", (vals) => {
					json = JSON.parse(vals);
					document.getElementById('timedate').innerHTML = json.timedate;
					document.getElementById('signal').innerHTML = json.signal;
					document.getElementById('pumpRelay').innerHTML = json.pumpstate ? "<u>ON</u>" : "OFF";
					document.getElementById('lightRelay').innerHTML = json.lightstate ? "<u>ON</u>" : "OFF";
					line1.append(new Date().getTime(), json.temperature);
					document.getElementById('temperature').innerHTML = json.temperature;
					line2.append(new Date().getTime(), json.humidity);
					document.getElementById('humidity').innerHTML = json.humidity;
				})
			}, 2000);

			smoothie.addTimeSeries(line1, { strokeStyle:'rgb(0, 255, 0)' , lineWidth:3});
			smoothie2.addTimeSeries(line2, { strokeStyle:'rgb(255, 0, 255)' , lineWidth:3});
		}

		});
	), pumpControl.interval, pumpControl.duration
	);

	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	Serial.println("");

	// Wait for connection
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
	}

	struct tm local;
	// ESP32 Systemzeit mit NTP Synchronisieren
	configTzTime(TZ_INFO, NTP_SERVER);
	getLocalTime(&local, 10000);
faketime(&local);

	if (MDNS.begin("esp32")) {
		Serial.println("MDNS responder started");
	}
delay(2000);

	server.on("/", handleRoot);
	server.on("/test.svg", drawGraph);
	server.on("/temperature1", handleTemperature1);
	server.on("/humidity", handleHumidity);
	server.on("/timedate", handleTimeDate);
	server.on("/signal", handleSignal);
	server.on("/pumpRelay", handlePumpRelay);
	server.on("/lightRelay", handleLightRelay);
	server.on("/combined", handleCombined);
	server.on("/esp-grow.js", []() {
		char msg[4096];
		Serial.println("JS");
		server.send(200, "application/javascript", jsPart);
	});
	server.on("/esp-grow.css", []() {
		char msg[4096];
		Serial.println("CSS");
		server.send(200, "application/css", cssPart);
	});
	server.on("/inline", []() {
		server.send(200, "text/plain", "this works as well");
	});
	server.onNotFound(handleNotFound);
	server.begin();
	Serial.println("HTTP server started");

	pinMode(pumpRelay, OUTPUT);
	digitalWrite(pumpRelay, HIGH);
	pinMode(lightRelay, OUTPUT);
	digitalWrite(lightRelay, HIGH);
	Serial.begin(115200);
	Serial.println(DHT_LIB_VERSION);

	if((local.tm_hour * 3600 + local.tm_min * 60) > lightControl.startTime_l
	&& (local.tm_hour * 3600 + local.tm_min * 60) < lightControl.startTime_l + lightControl.duration_l) {
		lightstate = 1;
		digitalWrite(lightRelay, !lightstate);
	}

}

int lastpump = -1;
int lastlight = -1;
void loop() {
	struct tm tm;

	if(millis() % 1000 == 0) {
		getLocalTime(&tm);
faketime(&tm);
		long now = (tm.tm_hour * 60 + tm.tm_min) * 60;

		if(tm.tm_min % pumpControl.interval == 0
		&& tm.tm_min != lastpump) {
			lastpump = tm.tm_min;
			pumpstate = 1;
			Serial.println("start pump");
			digitalWrite(pumpRelay, !pumpstate);
		}

		if(tm.tm_min == lastpump + pumpControl.duration
		&& tm.tm_min != lastpump) {
			lastpump = tm.tm_min;
			pumpstate = 0;
			Serial.println("stop pump");
			digitalWrite(pumpRelay, !pumpstate);
		}
		/*
		Serial.printf("min %d, last: %d, interval: %d, duration: %d\n", tm.tm_min,
		lastpump, pumpControl.interval, pumpControl.duration);
		*/

		if(now == lightControl.startTime_l
		&& tm.tm_min != lastlight) {
			lastlight = tm.tm_min;
			lightstate = 1;
			Serial.println("start light");
			digitalWrite(lightRelay, !lightstate);
//			Serial.printf("now: %d, startTime: %d, lightstate: %d\n", now, lightControl.startTime_l, lightstate);
		}

//Serial.printf("now: %d, startTime %d duration %d\n", now, lightControl.startTime_l, lightControl.duration_l);
		if(now == ((lightControl.startTime_l + lightControl.duration_l > 86400)
		? (lightControl.duration_l + lightControl.startTime_l) % 86400
		: lightControl.startTime_l + lightControl.duration_l)
		&& tm.tm_min != lastlight) {
			lastlight = tm.tm_min;
			lightstate = 0;
			Serial.println("stop light");
			digitalWrite(lightRelay, !lightstate);
		}

		/*
		if(tm.tm_sec % 5 == 0) {
			digitalWrite(lightRelay, lightstate);
			lightstate = !lightstate;
		}
		*/

		/*
		Serial.printf("free heap: %5.2f\nmin free heap: %5.2f\nget heap size: %5.2f\nget max alloc heap: %5.2f\n\n",
		ESP.getFreeHeap()/1024.0, ESP.getMinFreeHeap()/1024.0, ESP.getHeapSize()/1024.0, ESP.getMaxAllocHeap()/1024.0);
		*/
	}
	if(millis() % 2000 == 0) {
		signal = WiFi.RSSI();
		DHT_OLD = DHT;
RETRY:
		int chk = DHT.read22(DHT22_PIN);
		switch (chk) {
			case DHTLIB_OK:
				//Serial.print("OK,\t");
				break;
			case DHTLIB_ERROR_CHECKSUM:
				DHT = DHT_OLD;
				goto RETRY;
				Serial.print("Checksum error\n");
				break;
			case DHTLIB_ERROR_TIMEOUT:
				DHT = DHT_OLD;
				goto RETRY;
				Serial.print("Time out error\n");
				break;
			default:
				DHT = DHT_OLD;
				goto RETRY;
				Serial.print("Unknown error\n");
				break;
		}

		/*
		Serial.print("Signal strength: ");
		Serial.println(signal);
		Serial.printf("                                     \r");
		Serial.printf("humidity: %4.1f temperature: %4.1f\r", DHT.humidity, DHT.temperature);
		Serial.printf("DATE NOW: H=%d M=%d S=%d\n", tm.tm_hour, tm.tm_min , tm.tm_sec);
		Serial.printf("SEC OF DAY: %d\n", tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
		*/

	}
	server.handleClient();
}

void handlePumpRelay() {
	char message[30];
	switch(server.method()) {
		case HTTP_GET:
			snprintf(message, 30, "%d", pumpstate);
			server.send(200, "text/plain", message);
			break;
		case HTTP_POST:
			snprintf(message, 30, "bla blubb");
			if(server.hasArg("interval")) {
				pumpControl.interval = (long)(server.arg("interval")).toInt();
				//Serial.printf(">%d< %d\n", pumpControl.interval, pumpControl.duration);
				server.send(200, "text/plain", message);
			}
			if(server.hasArg("duration")) {
				pumpControl.duration = (long)(server.arg("duration")).toInt();
				//Serial.printf("%d >%d<\n", pumpControl.interval, pumpControl.duration);
				server.send(200, "text/plain", message);
			}
			updatePumpControl();
			break;
		default:
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}

char *timeToString(long t) {
	char *ret = (char *)malloc(6);
	struct tm *tm;
	tm = gmtime(&t);
//faketime(tm);
	sprintf(ret, "%02d:%02d", tm->tm_hour, tm->tm_min);
	return(ret);
}

void updateLightControl() {
	strncpy(lightControl.startTime_s, timeToString(lightControl.startTime_l), 6);
	strncpy(lightControl.duration_s, timeToString(lightControl.duration_l), 6);
	File f = SPIFFS.open("/lightctrl.dat", FILE_WRITE);
	f.write((byte *)&lightControl, sizeof(lightControl));
	f.close();
}

void updatePumpControl() {
	Serial.printf("save pumpControl, interval: %d duration: %d\n", pumpControl.interval, pumpControl.duration);
	File f = SPIFFS.open("/pumpctrl.dat", FILE_WRITE);
	f.write((byte *)&pumpControl, sizeof(pumpControl));
	f.close();
}

void handleLightRelay() {
	char message[30];
	switch(server.method()) {
		case HTTP_GET:
			snprintf(message, 30, "%d", lightstate);
			server.send(200, "text/plain", message);
			break;
		case HTTP_POST:
			snprintf(message, 30, "bla blubb");
			if(server.hasArg("startTime")) {
				Serial.print((server.arg("startTime")).toInt());
				lightControl.startTime_l = (long)(server.arg("startTime")).toInt() / 1000;
				// Serial.printf("%d %s\n", lightControl.startTime_l, timeToString(lightControl.startTime_l));
				server.send(200, "text/plain", message);
			}
			if(server.hasArg("duration")) {
				Serial.print((server.arg("duration")).toInt());
				lightControl.duration_l = (long)(server.arg("duration")).toInt() / 1000;
				// Serial.printf("%d %s\n", lightControl.duration_l, timeToString(lightControl.duration_l));
				server.send(200, "text/plain", message);
			}
			updateLightControl();
			break;
		default:
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}

void handleTemperature1() {
	char message[30];
	switch(server.method()) {
		case HTTP_GET:
			snprintf(message, 30, "%5.2f", DHT.temperature);
			server.send(200, "text/plain", message);
			break;
		default:
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}

void handleHumidity() {
	char message[30];
	switch(server.method()) {
		case HTTP_GET:
			snprintf(message, 30, "%5.2f", DHT.humidity);
			server.send(200, "text/plain", message);
			break;
		default:
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}

void handleSignal() {
	char message[30];
	switch(server.method()) {
		case HTTP_GET:
			snprintf(message, 30, "%d", signal);
			server.send(200, "text/plain", message);
			break;
		default:
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}

void handleTimeDate() {
	switch(server.method()) {
		case HTTP_GET:

			struct tm tm;
			getLocalTime(&tm, 10000);
faketime(&tm);

			char timestr[32];

			strftime(timestr, sizeof(timestr), "%F %T", &tm);
			server.send(200, "text/plain", timestr);
			break;
		default:
			char message[30];
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}

void faketime(struct tm *tm) {
	tm->tm_hour = 23;
	if(tm->tm_min < 55 && tm->tm_min > 5) {
		tm->tm_min = 55 + (tm->tm_min % 10);
		if(tm->tm_min >=60) {
			tm->tm_hour = 0;
			tm->tm_min = tm->tm_min % 60;
		}
	}
}
void handleCombined() {
	switch(server.method()) {
		case HTTP_GET:
			struct tm tm;
			DynamicJsonDocument val(256);
			getLocalTime(&tm, 10000);
faketime(&tm);

			char timestr[32];

			strftime(timestr, sizeof(timestr), "%F %T", &tm);

			val["timedate"] = timestr;
			val["signal"] = signal;
			val["humidity"] = DHT.humidity;
			val["temperature"] = DHT.temperature;
			val["pumpstate"] = pumpstate;
			val["lightstate"] = lightstate;
			String output;
			serializeJson(val, output);
			server.send(200, "application/json", output);
			break;
		default:
			char message[30];
			snprintf(message, 30, "not implemented");
			server.send(400, "text/plain", message);
			break;
	}
}



void handleNotFound() {
	digitalWrite(led, 1);
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += (server.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";

	for (uint8_t i = 0; i < server.args(); i++) {
		message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
	}

	server.send(404, "text/plain", message);
	digitalWrite(led, 0);
}


void drawGraph() {
	String out = "";
	char temp[100];
	out += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"400\" height=\"150\">\n";
	out += "<rect width=\"400\" height=\"150\" fill=\"rgb(250, 230, 210)\" stroke-width=\"1\" stroke=\"rgb(0, 0, 0)\" />\n";
	out += "<g stroke=\"black\">\n";
	for (int x = 1; x < 100; x += 1) {
		float y = temperature[x-1];
		float y2 = temperature[x];
		sprintf(temp, "<line x1=\"%d\" y1=\"%f\" x2=\"%d\" y2=\"%f\" stroke-width=\"1\" />\n", x, 140 - y, x + 10, 140 - y2);
		out += temp;
	}
	out += "</g>\n</svg>\n";

	server.send(200, "image/svg+xml", out);
}

void handleRoot() {
	digitalWrite(led, 1);
	char message[4096];
	struct tm local;
	getLocalTime(&local, 10000);
	char timestr[32];
	strftime(timestr, sizeof(timestr), "%F %T", &local);

	snprintf(message, 4096,
		STRINGIFY(<html>
		<head>
			<!-- <meta http-equiv='refresh' content='5'/> -->
    		<title>ESP32 - Grow Center</title>
    		<link rel="stylesheet" href="/esp-grow.css">
		</head>
		<body>
			<div id="header">
			<span id="header">ESP32 - Grow Center</span>
			<span class="right">
				Signal strength: <span id="signal">%d</span>
				&nbsp; &nbsp; &nbsp; &nbsp;
				<span id="timedate">%s</span></span>
			</span>
			</div>

			<div>
			<p>
			<span id="elem">Pump relay: <span id="pumpRelay">%s</span></span>
			<br>
			<span id="elem">Interval (min):
			<label>05:<input type="radio" name="interval" value="5"></label>
			<label>10:<input type="radio" name="interval" value="10"></label>
			<label>15:<input type="radio" name="interval" value="15"></label>
			<label>20:<input type="radio" name="interval" value="20"></label>
			<label>30:<input type="radio" name="interval" value="30"></label>
			</span>
			<br>
			<span id="elem">Duration (min):
			<label>05:<input type="radio" name="duration" value="5"></label>
			<label>10:<input type="radio" name="duration" value="10"></label>
			<label>15:<input type="radio" name="duration" value="15"></label>
			<label>20:<input type="radio" name="duration" value="20"></label>
			<label>30:<input type="radio" name="duration" value="30"></label>
			</span>
			<p>
			<span id="elem">Light relay: <span id="lightRelay">%s</span></span>
			<br>
			<span id="elem">Start time: <input type="time" id='starttime' value="%s" class="time"></span>
			<span id="elem">Duration: <input type="time" id='duration' value="%s" class="time"></span>
			</div>

			<div>
			<br>
			<span id="elem">temperature: <span id="temperature">%4.2f</span><br>
			<canvas class="chart" id="mycanvas" width="400" height="100"></canvas>
			</span>
			<p>
			<span id="elem">humidity: <span id="humidity">%4.2f</span><br>
			<canvas class="chart" id="mycanvas2" width="400" height="100"></canvas>
			</span>
			</div>
		</body>
		<script src="/esp-grow.js"></script>
		<script src="https://cdn.jsdelivr.net/timepicker.js/latest/timepicker.min.js"></script>
		<script src="https://cdnjs.cloudflare.com/ajax/libs/smoothie/1.34.0/smoothie.min.js"></script>
		</html>),
		signal,
		timestr,
		pumpstate ? "<u>ON</u>" : "OFF",
		lightstate ? "<u>ON</u>" : "OFF",
		lightControl.startTime_s,
		lightControl.duration_s,
		DHT.temperature,
		DHT.humidity
	);
	server.send(200, "text/html", message);
	digitalWrite(led, 0);
}


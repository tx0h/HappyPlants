#include <Arduino.h>
//#include <dhtnew.h>
#include <DHTSensor.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <stdio.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <time.h>


// temperature/humidity sensor pin, vars
#define DHT22_PIN 5
//DHTNEW DHT(5);
DHTSensor sensor(5);
struct {
	float temperature;
	float humidity;
} DHT;
int bufcnt;
float temperature_buf[20];
float humidity_buf[20];

// pump relay pin, state, struct
#define PUMPRELAY_PIN 18
int pumpstate = 0;
struct {
	long interval;
	long duration;
} pumpControl;

struct cycle {
	long cycleStart = 0;
	int totalDays = 0;
	int schemeStep = 1;
	float liter = 1;
} cycle;


// light relay pin & state
#define LIGHTRELAY_PIN 19
int lightstate = 0;
struct {
	long startTime_l;
	char startTime_s[6];
	long duration_l;
	char duration_s[6];
} lightControl;


// wifi signal strength
int wifiSignal = 0;


// wifi credentials (stored in /wificred.dat)
struct {
	char ssid[20];
	char password[20];
} wifiCredentials;


// instantiate webserver and websocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


// time management, the time comes over an ntp server
#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00"
struct tm local;

// best way to handle strings with linebreaks and quotes
#define STRINGIFY(...) #__VA_ARGS__

// playground led, pin state
#define LED_PIN 21
bool ledState = 0;


void saveCycleData() {
	Serial.printf("WRITE: cycleStart: %d, liter: %d, schemeStep: %d\n",cycle.cycleStart, cycle.liter, cycle.schemeStep);
	File f = SPIFFS.open("/cycledata.dat", FILE_WRITE);
	f.write((byte *)&cycle, sizeof(struct cycle));
	f.close();
	updateScheme_ws();
}

int getCycleLength() {
	static char ret[40];

	time_t now = time(NULL);
	if(SPIFFS.exists("/cycledata.dat")) {
		File f = SPIFFS.open("/cycledata.dat", FILE_READ);
		f.read((byte *)&cycle, sizeof(struct cycle));
		f.close();
		Serial.printf("LOADED: cycleStart: %d, liter: %d, schemeStep: %d\n",cycle.cycleStart, cycle.liter, cycle.schemeStep);
	} else {
		cycle.cycleStart = now;
		cycle.totalDays = 0;
		cycle.liter = 1;
		cycle.schemeStep = 1;
		saveCycleData();
		return(cycle.totalDays);
	}

	time_t diff = now - cycle.cycleStart;

	struct tm *tm_diff = gmtime(&diff);
	cycle.totalDays = tm_diff->tm_yday;
/*
    cycle.weeks = tm_diff->tm_yday / 7;
    cycle.days = cycle.weeks - ((cycle.weeks / 7) * 7);


	if(!cycle.weeks) {
		sprintf(ret, "%d days", cycle.totalDays);
	} else {
		if(!cycle.days) {
			sprintf(ret, "%d days or %d weeks", cycle.totalDays, cycle.weeks);
		} else {
			sprintf(ret, "%d days or %d weeks and %d days", cycle.totalDays, cycle.weeks, cycle.days);
		}
	}
*/
	return(cycle.totalDays);
}

void updateLedState() {
	Serial.println("notify clients");
	DynamicJsonDocument val(256);
	val["type"] = "toggle";
	val["state"] = ledState;
	digitalWrite(LED_PIN, ledState);
	String output;
	serializeJson(val, output);
	ws.textAll(output);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
	AwsFrameInfo *info = (AwsFrameInfo*)arg;
	if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

		StaticJsonDocument<256> json;
		data[len] = 0;

		Serial.printf("some websocket message: >>%s<<\n", data);
		DeserializationError error = deserializeJson(json, data);
		if(error) {
			Serial.printf("not really json: %s\n", data);

			if (strcmp((char*)data, "reset") == 0) {
				ESP.restart();
			}
		} else {
			Serial.printf("json handler: %s\n", data);
			if(!strcmp(json["type"], "toggle")) {
				ledState = !ledState;
				updateLedState();
			}
			if(!strcmp(json["type"], "schemeStep")) {
				cycle.schemeStep = json["schemeStep"];
				saveCycleData();
			}
			if(!strcmp(json["type"], "literChanged")) {
				cycle.liter = json["liter"];
				saveCycleData();
			}
			if(!strcmp(json["type"], "resetCycle")) {
				cycle.cycleStart = time(NULL);
				cycle.totalDays = 0;
				cycle.liter = 1;
				cycle.schemeStep = 1;
				saveCycleData();
			}
		}
	}
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
	Serial.println("on event");
	switch (type) {
		case WS_EVT_CONNECT:
			updateScheme_ws();
			Serial.printf("WebSocket client #%u connected from %s\n",
				client->id(), client->remoteIP().toString().c_str());
			break;
		case WS_EVT_DISCONNECT:
			Serial.printf("WebSocket client #%u disconnected\n", client->id());
			break;
		case WS_EVT_DATA:
			handleWebSocketMessage(arg, data, len);
			break;
		case WS_EVT_PONG:
		case WS_EVT_ERROR:
			break;
	}
}

void handleRoot(AsyncWebServerRequest *request) {
	digitalWrite(LED_PIN, ledState);
	char message[4096];
	getLocalTime(&local, 10000);
	char timestr[32];
	strftime(timestr, sizeof(timestr), "%F %T", &local);

	snprintf(message, 4096,
		STRINGIFY(<html>
		<head>
			<!-- <meta http-equiv='refresh' content='5'/> -->
    		<title>Happy Plants</title>
    		<link rel="stylesheet" href="/happyPlants.css" />
    		<link rel="icon" type="image/svg+xml" href="/favicon.svg" sizes="any">
		</head>
		<body>
			<div id="header">
			<span id="header">Happy Plants</span>
			<span class="right">
				<input type="button" id="toggle" value="T">
				<input type="button" id="reset" value="R">
				Signal strength: <span id="wifiSignal">%d</span>
				&nbsp; &nbsp; &nbsp; &nbsp;
				<span id="timedate">%s</span></span>
			</span>
			</div>

			<div>
			<br>
			<span id="left">
			<span id="elem">Pump relay: <span id="pumpRelay">%s</span></span>
			<br>
			<span id="elem">Interval (min):
			<input type="radio" name="interval" id="1" value="5"><label for="1">05</label>
			<input type="radio" name="interval" id="2" value="10"><label for="2">10</label>
			<input type="radio" name="interval" id="3" value="15"><label for="3">15</label>
			<input type="radio" name="interval" id="4" value="20"><label for="4">20</label>
			<input type="radio" name="interval" id="5" value="30"><label for="5">30</label>
			</span>
			<br>
			<span id="elem">Duration (min):
			<input type="radio" name="duration" id="a" value="5"><label for="a">05</label>
			<input type="radio" name="duration" id="b" value="10"><label for="b">10</label>
			<input type="radio" name="duration" id="c" value="15"><label for="c">15</label>
			<input type="radio" name="duration" id="d" value="20"><label for="d">20</label>
			<input type="radio" name="duration" id="e" value="30"><label for="e">30</label>
			</span>

			<br>
			<br>
			<span id="elem">Light relay: <span id="lightRelay">%s</span></span>
			<br>
			<span id="elem">Start time: <input type="time" id='starttime' value="%s" class="time"></span>
			<span id="elem">Duration: <input type="time" id='duration' value="%s" class="time"></span>

			<br>
			<br>
			<span id="elem">temperature: <span id="temperature">%4.2f</span><br>
			<canvas class="chart" id="mycanvas" width="400" height="100"></canvas>
			</span>

			<br>
			<br>
			<span id="elem">humidity: <span id="humidity">%4.2f</span><br>
			<canvas class="chart" id="mycanvas2" width="400" height="100"></canvas>
			</span>
			</span>

			<span id="right">
				Cycle run since: <span id="cyclelen">%d</span>
			</span>
			</div>
			<script>
				externalPumpInterval=%d;
				externalPumpDuration=%d;
			</script>
		</body>
		<script src="/happyPlants.js"></script>
		<script src="https://cdnjs.cloudflare.com/ajax/libs/smoothie/1.34.0/smoothie.min.js"></script>
		</html>),
		wifiSignal,
		timestr,
		pumpstate ? "<u>ON</u>" : "OFF",
		lightstate ? "<u>ON</u>" : "OFF",
		lightControl.startTime_s,
		lightControl.duration_s,
		DHT.temperature,
		DHT.humidity,
		getCycleLength(),
		pumpControl.interval,
		pumpControl.duration
	);
	request->send(200, "text/html", message);

	digitalWrite(LED_PIN, ledState = 0);
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
	if(!index) {
		Serial.println("UploadStart: " + filename);
		// open the file on first call and store the file handle in the request object
		request->_tempFile = SPIFFS.open("/"+filename, "w");
	}
	if(len) {
		// stream the incoming chunk to the opened file
		request->_tempFile.write(data,len);
	}
	if(final) {
		Serial.println("UploadEnd: " + filename + ",size: " + index+len);
		// close the file handle as the upload is now done
		request->_tempFile.close();
//		request->redirect("/");
	}
}

void handlePumpRelay(AsyncWebServerRequest *request) {
	char message[30];
	switch(request->method()) {
		case HTTP_GET:
			snprintf(message, 30, "%d", pumpstate);
			request->send(200, "text/plain", message);
			break;
		case HTTP_POST:
			snprintf(message, 30, "bla blubb");
			if(request->hasParam("interval", true)) {
				pumpControl.interval = (long)(request->getParam("interval", true)->value()).toInt();
				Serial.printf(">%d< %d\n", pumpControl.interval, pumpControl.duration);
				request->send(200, "text/plain", message);
			}
			if(request->hasParam("duration", true)) {
				pumpControl.duration = (long)(request->getParam("duration", true)->value()).toInt();
				Serial.printf("%d >%d<\n", pumpControl.interval, pumpControl.duration);
				request->send(200, "text/plain", message);
			}
			updatePumpControl();
			break;
		default:
			snprintf(message, 30, "not implemented");
			request->send(400, "text/plain", message);
			break;
	}
}

char *timeToString(long t) {
	char *ret = (char *)malloc(6);
	struct tm *tm;
	tm = gmtime(&t);
	sprintf(ret, "%02d:%02d", tm->tm_hour, tm->tm_min);
	return(ret);
}

void updateLightControl() {
	strncpy(lightControl.startTime_s, timeToString(lightControl.startTime_l), 6);
	strncpy(lightControl.duration_s, timeToString(lightControl.duration_l), 6);

	Serial.printf("save lightControl, starttime: %s, duration: %s\n",
		lightControl.startTime_s, lightControl.duration_s);

	File f = SPIFFS.open("/lightctrl.dat", FILE_WRITE);
	f.write((byte *)&lightControl, sizeof(lightControl));
	f.close();
	updateLight_ws();
}

void updatePumpControl() {
	Serial.printf("save pumpControl, interval: %d duration: %d\n",
		pumpControl.interval, pumpControl.duration);

	File f = SPIFFS.open("/pumpctrl.dat", FILE_WRITE);
	f.write((byte *)&pumpControl, sizeof(pumpControl));
	f.close();
	updatePump_ws();
}

void handleLightRelay(AsyncWebServerRequest *request) {
	char message[30];
	switch(request->method()) {
		case HTTP_GET:
			snprintf(message, 30, "%d", lightstate);
			request->send(200, "text/plain", message);
			break;
		case HTTP_POST:
			snprintf(message, 30, "bla blubb");
			if(request->hasParam("startTime", true)) {
				Serial.print(request->getParam("startTime", true)->value().toInt());
				lightControl.startTime_l = (long)request->getParam("startTime", true)->value().toInt() / 1000;
				// Serial.printf("%d %s\n", lightControl.startTime_l, timeToString(lightControl.startTime_l));
				request->send(200, "text/plain", message);
			}
			if(request->hasParam("duration", true)) {
				Serial.print(request->getParam("duration", true)->value().toInt());
				lightControl.duration_l = (long)request->getParam("duration", true)->value().toInt() / 1000;
				// Serial.printf("%d %s\n", lightControl.duration_l, timeToString(lightControl.duration_l));
				request->send(200, "text/plain", message);
			}
			updateLightControl();
			break;
		default:
			snprintf(message, 30, "not implemented");
			request->send(400, "text/plain", message);
			break;
	}
}

void handleTemperature1(AsyncWebServerRequest *request) {
	char message[30];
	switch(request->method()) {
		case HTTP_GET:
			snprintf(message, 30, "%5.2f", DHT.temperature);
			request->send(200, "text/plain", message);
			break;
		default:
			snprintf(message, 30, "not implemented");
			request->send(400, "text/plain", message);
			break;
	}
}

void handleHumidity(AsyncWebServerRequest *request) {
	char message[30];
	switch(request->method()) {
		case HTTP_GET:
			snprintf(message, 30, "%5.2f", DHT.humidity);
			request->send(200, "text/plain", message);
			break;
		default:
			snprintf(message, 30, "not implemented");
			request->send(400, "text/plain", message);
			break;
	}
}

void handleSignal(AsyncWebServerRequest *request) {
	char message[30];
	switch(request->method()) {
		case HTTP_GET:
			snprintf(message, 30, "%d", wifiSignal);
			request->send(200, "text/plain", message);
			break;
		default:
			snprintf(message, 30, "not implemented");
			request->send(400, "text/plain", message);
			break;
	}
}

void handleTimeDate(AsyncWebServerRequest *request) {
	switch(request->method()) {
		case HTTP_GET:

			struct tm tm;
			getLocalTime(&tm, 10000);
			char timestr[32];

			strftime(timestr, sizeof(timestr), "%F %T", &tm);
			request->send(200, "text/plain", timestr);
			break;
		default:
			char message[30];
			snprintf(message, 30, "not implemented");
			request->send(400, "text/plain", message);
			break;
	}
}

void updatePump_ws() {
	DynamicJsonDocument val(256);
	val["type"] = "updatePump";
	val["pumpinterval"] = pumpControl.interval;
	val["pumpduration"] = pumpControl.duration;
	String output;
	serializeJson(val, output);
	ws.textAll(output);
}

void updateLight_ws() {
	DynamicJsonDocument val(256);
	val["type"] = "updateLight";
	val["lightstart"] = lightControl.startTime_s;
	val["lightduration"] = lightControl.duration_s;
	String output;
	serializeJson(val, output);
	ws.textAll(output);
}

void updateScheme_ws() {
	DynamicJsonDocument val(256);
	val["type"] = "updateScheme";
	val["cycleLength"] = getCycleLength();
	val["schemeStep"] = cycle.schemeStep;
	val["liter"] = cycle.liter;
	String output;
	serializeJson(val, output);
	ws.textAll(output);
}

void update_ws() {
	struct tm tm;
	DynamicJsonDocument val(256);
	getLocalTime(&tm, 10000);
	char timestr[32];

	strftime(timestr, sizeof(timestr), "%F %T", &tm);

	val["type"] = "update";
	val["timedate"] = timestr;
	val["wifiSignal"] = wifiSignal;
	val["humidity"] = DHT.humidity;
	val["temperature"] = DHT.temperature;
	val["pumpstate"] = pumpstate;
	val["lightstate"] = lightstate;
	String output;
	serializeJson(val, output);
	ws.textAll(output);
}

void handleNotFound(AsyncWebServerRequest *request) {
	digitalWrite(LED_PIN, ledState = 1);
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += request->url();
	message += "\nMethod: ";
	message += (request->method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += request->args();
	message += "\n";

	for (uint8_t i = 0; i < request->args(); i++) {
		message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
	}

	request->send(404, "text/plain", message);
	digitalWrite(LED_PIN, ledState = 0);
}


void startSPIFFS() {
	if(SPIFFS.begin(true)) {
		Serial.println("SPIFFS mounted");
	} else {
		Serial.println("error mount SPIFFS");
	}

    // Get all information of SPIFFS
	//SPIFFS.format();
    //SPIFFS.remove("/happyPlants.js");
    //SPIFFS.remove("/happyPlants.css");
	unsigned int total = SPIFFS.totalBytes();
	unsigned int used = SPIFFS.usedBytes();

	Serial.println("===== File system info =====");
	Serial.printf("total space: %d byte; used space: %d bytes\n", total, used);

//	SPIFFS.remove("/wificred.dat");
	if(SPIFFS.exists("/wificred.dat")) {
		File f = SPIFFS.open("/wificred.dat", FILE_READ);
		f.read((byte *)&wifiCredentials, sizeof(wifiCredentials));
		f.close();
	} else {
		strcpy(wifiCredentials.ssid, "essid");
		strcpy(wifiCredentials.password, "pass");
		File f = SPIFFS.open("/wificred.dat", FILE_WRITE);
		f.write((byte *)&wifiCredentials, sizeof(wifiCredentials));
		f.close();
	}

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
}

void startWebServer() {
	server.on("/", handleRoot);
	server.on("/temperature1", handleTemperature1);
	server.on("/humidity", handleHumidity);
	server.on("/timedate", handleTimeDate);
	server.on("/wifiSignal", handleSignal);
	server.on("/pumpRelay", handlePumpRelay);
	server.on("/lightRelay", handleLightRelay);
	server.on("/bla.ttf", [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/bla.ttf", "application/x-font-ttf");
	});
	server.on("/happyPlants.js", [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/happyPlants.js", "application/x-javascript");
	});
	server.on("/happyPlants.css", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/happyPlants.css", "text/css");
	});
	server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
Serial.println("send favicon.svg, wth!");
		request->send(SPIFFS, "/favicon.svg", "image/svg+xml");
	});
	server.on("/inline", [](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", "this works as well");
	});

	//erver.onFileUpload(handleUpload);
	server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
		request->send(200);
	}, handleUpload);

	server.begin();
	Serial.println("HTTP server started");
}

void startWebSocket() {
	ws.onEvent(onEvent);
	server.addHandler(&ws);
}


void startWiFi() {
	WiFi.mode(WIFI_STA);
	WiFi.begin(wifiCredentials.ssid, wifiCredentials.password);
	Serial.println("");

	// Wait for connection
	while (WiFi.status() != WL_CONNECTED) {
		delay(200);
	}

	// ESP32 Systemzeit mit NTP Synchronisieren
	configTzTime(TZ_INFO, NTP_SERVER);
	getLocalTime(&local, 10000);
}

void setup() {

//	DHT.read();
	DHTSensorMeasurement re = sensor.Read();
	DHT.temperature = re.TemperatureInCelsius();
	DHT.humidity = re.Humidity();
	bufcnt=0;
	for(int i=0; i<=19; i++) {
		temperature_buf[i] = DHT.temperature;
		humidity_buf[i] = DHT.humidity;
	}
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, ledState = 0);

	pinMode(PUMPRELAY_PIN, OUTPUT);
	digitalWrite(PUMPRELAY_PIN, HIGH);

	pinMode(LIGHTRELAY_PIN, OUTPUT);
	digitalWrite(LIGHTRELAY_PIN, HIGH);


	Serial.begin(115200);
	Serial.println("*** OK.");
//	Serial.println(DHT_LIB_VERSION);

	delay(200);


//	server.onNotFound(handleNotFound);
	Serial.println("Start SPIFFS");
	startSPIFFS();
	delay(200);
	Serial.println("Start WiFi");
	startWiFi();
	delay(200);
	Serial.println("Start WebServer");
	startWebServer();
	delay(200);
	Serial.println("Start WebSocket");
	startWebSocket();
	delay(200);

	if((local.tm_hour * 3600 + local.tm_min * 60) > lightControl.startTime_l
	&& (local.tm_hour * 3600 + local.tm_min * 60) < lightControl.startTime_l + lightControl.duration_l) {
		lightstate = 1;
		digitalWrite(LIGHTRELAY_PIN, !lightstate);
	}

}

int lastpump = -1;
int lastlight = -1;
void loop() {
	struct tm tm;

	if(millis() % 1000 == 0) {
		getLocalTime(&tm);
		long now = (tm.tm_hour * 60 + tm.tm_min) * 60;

		if(tm.tm_min % pumpControl.interval == 0
		&& tm.tm_min != lastpump) {
			lastpump = tm.tm_min;
			pumpstate = 1;
			Serial.println("start pump");
			digitalWrite(PUMPRELAY_PIN, !pumpstate);
		}

		if(tm.tm_min == lastpump + pumpControl.duration
		&& tm.tm_min != lastpump) {
			lastpump = tm.tm_min;
			pumpstate = 0;
			Serial.println("stop pump");
			digitalWrite(PUMPRELAY_PIN, !pumpstate);
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
			digitalWrite(LIGHTRELAY_PIN, !lightstate);
//			Serial.printf("now: %d, startTime: %d, lightstate: %d\n", now, lightControl.startTime_l, lightstate);
		}

//Serial.printf("now: %d, startTime %d duration %d\n", now, lightControl.startTime_l, lightControl.duration_l);
		if(((now == ((lightControl.startTime_l + lightControl.duration_l > 86400)
		? (lightControl.duration_l + lightControl.startTime_l) % 86400
		: lightControl.startTime_l + lightControl.duration_l))
		|| (now == 0 && lightControl.startTime_l + lightControl.duration_l == 86400))
		&& tm.tm_min != lastlight) {
			lastlight = tm.tm_min;
			lightstate = 0;
			Serial.println("stop light");
			digitalWrite(LIGHTRELAY_PIN, !lightstate);
		}

		/*
		Serial.printf("free heap: %5.2f\nmin free heap: %5.2f\nget heap size: %5.2f\nget max alloc heap: %5.2f\n\n",
		ESP.getFreeHeap()/1024.0, ESP.getMinFreeHeap()/1024.0, ESP.getHeapSize()/1024.0, ESP.getMaxAllocHeap()/1024.0);
		*/
	}

	// every second second, read sensors, inform the clients
	if(millis() % 5000 == 0) {

		wifiSignal = WiFi.RSSI();
		int retry = 5;

RETRY:
		//int chk = DHT.read22(DHT22_PIN);
//		int chk = DHT.read();
		DHTSensorMeasurement re = sensor.Read();
		DHT.temperature = re.TemperatureInCelsius();
		DHT.humidity = re.Humidity();
		float temperature = 0;
		float humidity = 0;
		/*
		switch (chk) {
			case DHTLIB_OK:
				//Serial.print("OK,\t");
				if(DHT.humidity > 1000 && --retry)
					goto RETRY;
				break;
			case DHTLIB_ERROR_CHECKSUM:
				if(--retry)
					goto RETRY;
				break;
			case DHTLIB_ERROR_TIMEOUT:
				if(--retry)
					goto RETRY;
				break;
			default:
				if(--retry)
					goto RETRY;
				break;
		}
		*/
		temperature_buf[bufcnt] = DHT.temperature;
		humidity_buf[bufcnt] = DHT.humidity;
		if(++bufcnt >= 20) {
			bufcnt = 0;
		}
		for(int i = 0; i <= 19; i++) {
			temperature += temperature_buf[i];
			humidity += humidity_buf[i];
		}
		DHT.temperature = temperature / 20;
		DHT.humidity = humidity / 20;

		//Serial.printf("Memory: %.2f/%.2f\n", ESP.getFreeHeap() / 1024.0, ESP.getHeapSize() / 1024.0);
		// update the websocket clients
		update_ws();
	}

	if(millis() % 3600000 == 0) {
		updateScheme_ws();
	}
}

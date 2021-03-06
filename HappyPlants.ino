#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <stdio.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <time.h>


#define DEBUG 1

struct environment {
	float temperature;
	float humidity;
	float temperature_buf[20];
	float humidity_buf[20];
	int bufcnt;
} environment;

// temperature/humidity sensor pin, vars
#undef USE_DHT
#ifdef USE_DHT

#include <DHTSensor.h>
#define DHT22_PIN 5
DHTSensor sensor(5);

#endif


#define USE_BME280
#ifdef USE_BME280

#include <Bme280BoschWrapper.h>
Bme280BoschWrapper bme280(true);

#endif

#define LIGHTSENSOR_PIN 36
unsigned int lightsensor;

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
	int daysPerStep[7];
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


// time management, the time comes from an ntp server
#define NTP_SERVER "de.pool.ntp.org"
#define TZ_INFO "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00"
struct tm local;

// best way to handle strings with linebreaks and quotes
#define STRINGIFY(...) #__VA_ARGS__

// playground led, pin state
#define LED_PIN 2
bool ledState = 0;

int rebootNow = 0;


void saveCycleData() {
#ifdef DEBUG
	Serial.printf("WRITE: cycleStart: %d, liter: %d, schemeStep: %d\n",cycle.cycleStart, cycle.liter, cycle.schemeStep);
#endif
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
#ifdef DEBUG
		Serial.printf("LOADED: cycleStart: %d, liter: %f, schemeStep: %d\n",cycle.cycleStart, cycle.liter, cycle.schemeStep);
#endif
	} else {
		cycle.cycleStart = now;
		cycle.totalDays = 0;
		cycle.liter = 1;
		cycle.schemeStep = 1;
		for(int i=0; i < 7; i++) {
			cycle.daysPerStep[i] = 0;
		}
		saveCycleData();
		return(cycle.totalDays);
	}

	time_t diff = now - cycle.cycleStart;
	struct tm *tm_diff = gmtime(&diff);

	if(tm_diff->tm_yday > cycle.totalDays) {
		cycle.daysPerStep[cycle.schemeStep-1] += tm_diff->tm_yday - cycle.totalDays;
		cycle.totalDays = tm_diff->tm_yday;
		saveCycleData();
	} else {
		cycle.totalDays = tm_diff->tm_yday;
	}

	return(cycle.totalDays);
}

void updateLedState() {
#ifdef DEBUG
	Serial.println("notify clients");
#endif
	DynamicJsonDocument val(256);
	val["type"] = "toggle";
	val["state"] = ledState;
	digitalWrite(LED_PIN, ledState);
	digitalWrite(LIGHTRELAY_PIN, lightstate = !lightstate);
	String output;
	serializeJson(val, output);
	ws.textAll(output);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
	AwsFrameInfo *info = (AwsFrameInfo*)arg;
	if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

		StaticJsonDocument<256> json;
		data[len] = 0;
#if defined(DEBUG) && (DEBUG > 1)
		Serial.printf("some websocket message: >>%s<<\n", data);
#endif
		DeserializationError error = deserializeJson(json, data);
		if(error) {
			Serial.printf("not really json: %s\n", data);

			if (strcmp((char*)data, "reset") == 0) {
				ESP.restart();
			}
		} else {
#if defined(DEBUG) && (DEBUG > 1)
			Serial.printf("json handler: %s\n", data);
#endif
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
				for(int i=0; i < 7; i++) {
					cycle.daysPerStep[i] = 0;
				}
				saveCycleData();
			}
		}
	}
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {

#if defined(DEBUG) && (DEBUG > 1)
	Serial.println("on event");
#endif

	switch (type) {
		case WS_EVT_CONNECT:
			updateScheme_ws();
#if defined(DEBUG) && (DEBUG > 1)
			Serial.printf("WebSocket client #%u connected from %s\n",
				client->id(), client->remoteIP().toString().c_str());
#endif
			break;
		case WS_EVT_DISCONNECT:
#if defined(DEBUG) && (DEBUG > 1)
			Serial.printf("WebSocket client #%u disconnected\n", client->id());
#endif
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
				<input type="button" id="reset" value="Reset">
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
			<canvas class="chart" id="mycanvas1" width="400" height="100"></canvas>
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
		environment.temperature,
		environment.humidity,
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

void handleUpgrade(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	if (!index) {
		Serial.printf("Update Start: %s\n", filename.c_str());
		if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
			Update.printError(Serial);
			request->send(200, "text/plain", "\n********\n*** ERROR\n");
		}
	}
	if (!Update.hasError()) {
		if (Update.write(data, len) != len) {
			Update.printError(Serial);
			request->send(200, "text/plain", "\n********\n*** ERROR\n");
		}
	}
	if(final) {
		if (Update.end(true)) {
			Serial.printf("Update Success: %uB\n", index + len);
			request->send(200, "text/plain", "\n********\nALL FINE.\n");
			delay(500);
		} else {
			Update.printError(Serial);
			request->send(200, "text/plain", "\n********\n*** ERROR\n");
		}
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
#if defined(DEBUG) && (DEBUG > 1)
				Serial.printf(">%d< %d\n", pumpControl.interval, pumpControl.duration);
#endif
				request->send(200, "text/plain", message);
			}
			if(request->hasParam("duration", true)) {
				pumpControl.duration = (long)(request->getParam("duration", true)->value()).toInt();
#if defined(DEBUG) && (DEBUG > 1)
				Serial.printf("%d >%d<\n", pumpControl.interval, pumpControl.duration);
#endif
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
			snprintf(message, 30, "OK");
			if(request->hasParam("startTime", true)) {
				lightControl.startTime_l = (long)request->getParam("startTime", true)->value().toInt() / 1000;
#if defined(DEBUG) && (DEBUG > 1)
				Serial.print(request->getParam("startTime", true)->value().toInt());
				Serial.printf("%d %s\n", lightControl.startTime_l, timeToString(lightControl.startTime_l));
#endif
				request->send(200, "text/plain", message);
			}
			if(request->hasParam("duration", true)) {
				lightControl.duration_l = (long)request->getParam("duration", true)->value().toInt() / 1000;
#if defined(DEBUG) && (DEBUG > 1)
				Serial.print(request->getParam("duration", true)->value().toInt());
				Serial.printf("%d %s\n", lightControl.duration_l, timeToString(lightControl.duration_l));
#endif
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
			snprintf(message, 30, "%5.2f", environment.temperature);
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
			snprintf(message, 30, "%5.2f", environment.humidity);
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
	DynamicJsonDocument arr(256);

	val["type"] = "updateScheme";
	val["cycleLength"] = getCycleLength();
	copyArray(cycle.daysPerStep, arr);
	val["daysPerStep"] = arr;
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
	val["humidity"] = environment.humidity;
	val["temperature"] = environment.temperature;
	val["pumpstate"] = pumpstate;
	val["lightstate"] = lightstate;
	val["lightsensor"] = lightsensor;
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

	if(SPIFFS.exists(request->url())) {
		request->send(SPIFFS, request->url(), "application/x-binary");
	} else {
		request->send(404, "text/plain", message);
		Serial.println("404 --> "+message+", "+request->url());
	}
	digitalWrite(LED_PIN, ledState = 0);
}


void startSPIFFS() {
	if(SPIFFS.begin(true)) {
		Serial.println("SPIFFS mounted");
	} else {
		Serial.println("error mount SPIFFS");
	}

    // Get all information of SPIFFS
	unsigned int total = SPIFFS.totalBytes();
	unsigned int used = SPIFFS.usedBytes();

#ifdef DEBUG
	Serial.println("===== File system info =====");
	Serial.printf("total space: %d byte; used space: %d bytes\n", total, used);
#endif

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
#ifdef DEBUG
		Serial.printf("start light: %s\nlight duration: %s\n", lightControl.startTime_s, lightControl.duration_s);
#endif
	} else {
		lightControl.startTime_l = 3600 * 6;
		lightControl.duration_l = 3600 * 18;
		updateLightControl();
	}

	if(SPIFFS.exists("/pumpctrl.dat")) {
		File f = SPIFFS.open("/pumpctrl.dat", FILE_READ);
		f.read((byte *)&pumpControl, sizeof(pumpControl));
		f.close();
#ifdef DEBUG
		Serial.printf("pump interval: %d\npump duration: %d\n", pumpControl.interval, pumpControl.duration);
#endif
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
	server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/favicon.svg", "image/svg+xml");
	});
	server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(SPIFFS, "/favicon.svg", "image/svg+xml");
	});
	server.on("/inline", [](AsyncWebServerRequest *request) {
		request->send(200, "text/plain", "this works as well");
	});

	//erver.onFileUpload(handleUpload);
	server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
		request->send(200);
	}, handleUpload);


	server.on("/upgrade", HTTP_POST, [](AsyncWebServerRequest *request) {
		rebootNow = !Update.hasError();
		AsyncWebServerResponse *response =
			request->beginResponse(200, "text/html", rebootNow ? "<h1><strong>Update DONE</strong></h1><br><a href='/'>Return Home</a>" : "<h1><strong>Update FAILED</strong></h1><br><a href='/updt'>Retry?</a>");
			response->addHeader("Connection", "close");
			request->send(response);

		},
		handleUpgrade
	);

	server.onNotFound(handleNotFound);

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

	Serial.begin(115200);
	Serial.println("*** OK.");
#ifdef DEBUG
	Serial.printf("sketch size:%d\nsketch md5sum: %s\nfree sketch space: %d\n",
		ESP.getSketchSize(), ESP.getSketchMD5(), ESP.getFreeSketchSpace());
#endif

#ifdef USE_DHT
	DHTSensorMeasurement re = sensor.Read();
	environment.temperature = re.TemperatureInCelsius();
	environment.humidity = re.Humidity();
#endif

#ifdef USE_BME280
	while(!bme280.beginI2C(0x76)) {
		Serial.println("Cannot find sensor.");
		delay(1000);
	}
	bme280.measure();
	environment.temperature = bme280.getTemperature() / 100.0;
	environment.humidity = bme280.getHumidity() / 1024.0;
#endif

	environment.bufcnt = 0;
	for(int i=0; i<=19; i++) {
		environment.temperature_buf[i] = environment.temperature;
		environment.humidity_buf[i] = environment.humidity;
	}

	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, ledState = 0);

	pinMode(PUMPRELAY_PIN, OUTPUT);
	digitalWrite(PUMPRELAY_PIN, HIGH);
	delay(100);

	pinMode(LIGHTRELAY_PIN, OUTPUT);
	digitalWrite(LIGHTRELAY_PIN, HIGH);
	delay(100);

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

	// should the light be on or off?
	if((local.tm_hour * 3600 + local.tm_min * 60) > lightControl.startTime_l
	&& (local.tm_hour * 3600 + local.tm_min * 60) < lightControl.startTime_l + lightControl.duration_l) {
		lightstate = 1;
		digitalWrite(LIGHTRELAY_PIN, !lightstate);
	}
}

int lastpump = -1;
int lastlight = -1;
int lastsec = -1;
void loop() {
	struct tm tm;

	if (rebootNow) {
		Serial.println("Restart");
		ESP.restart();
	}

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

#if defined(DEBUG) && (DEBUG > 1)
		Serial.printf("min %d, last: %d, interval: %d, duration: %d\n", tm.tm_min,
		lastpump, pumpControl.interval, pumpControl.duration);
#endif

		if(now == lightControl.startTime_l
		&& micros() != lastlight) {
			lastlight = micros();
			lightstate = 1;
			Serial.println("start light");
			digitalWrite(LIGHTRELAY_PIN, !lightstate);
#if defined(DEBUG) && (DEBUG > 1)
			Serial.printf("now: %d, startTime: %d, lightstate: %d\n", now, lightControl.startTime_l, lightstate);
#endif
		}

//Serial.printf("now: %d, startTime %d duration %d\n", now, lightControl.startTime_l, lightControl.duration_l);
		if(((now == ((lightControl.startTime_l + lightControl.duration_l > 86400)
		? (lightControl.duration_l + lightControl.startTime_l) % 86400
		: lightControl.startTime_l + lightControl.duration_l))
		|| (now == 0 && lightControl.startTime_l + lightControl.duration_l == 86400))
		&& tm.tm_min != lastlight) {
			lastlight = -1;
			lightstate = 0;
			Serial.println("stop light");
			digitalWrite(LIGHTRELAY_PIN, !lightstate);
		}

#if defined(DEBUG) && (DEBUG > 1)
		Serial.printf("free heap: %5.2f\nmin free heap: %5.2f\nget heap size: %5.2f\nget max alloc heap: %5.2f\n\n",
		ESP.getFreeHeap()/1024.0, ESP.getMinFreeHeap()/1024.0, ESP.getHeapSize()/1024.0, ESP.getMaxAllocHeap()/1024.0);
#endif
	}

	// every second second, read sensors, inform the clients
	if(millis() % 5000 == 0) {

		wifiSignal = WiFi.RSSI();
		int retry = 5;

		if(tm.tm_sec != lastsec) {
			lightsensor = analogRead(LIGHTSENSOR_PIN);
		}

#ifdef USE_DHT
		DHTSensorMeasurement re = sensor.Read();
		environment.temperature = re.TemperatureInCelsius();
		environment.humidity = re.Humidity();
#if defined(DEBUG) && (DEBUG > 1)
		Serial.printf("dht temperature: %.2f and humidity: %.2f\n",
		environment.temperature, environment.humidity);
#endif
#endif

#ifdef USE_BME280
		bme280.measure();
		environment.temperature = bme280.getTemperature() / 100.0;
		environment.humidity = bme280.getHumidity() / 1024.0;
#if defined(DEBUG) && (DEBUG > 1)
		Serial.printf("bme280 temperature: %.2f, humidity: %.2f and preasure: %.2f\n",
		environment.temperature, environment.humidity, bme280.getPressure()/100.0);
#endif
#endif

		environment.temperature_buf[environment.bufcnt] = environment.temperature;
		environment.humidity_buf[environment.bufcnt] = environment.humidity;
		if(++environment.bufcnt >= 20) {
			environment.bufcnt = 0;
		}
		for(int i = 0; i <= 19; i++) {
			environment.temperature += environment.temperature_buf[i];
			environment.humidity += environment.humidity_buf[i];
		}
		environment.temperature /= 20;
		environment.humidity /= 20;

#if defined(DEBUG) && (DEBUG > 1)
		Serial.printf("Memory: %.2f/%.2f\n", ESP.getFreeHeap() / 1024.0, ESP.getHeapSize() / 1024.0);
#endif

		lastsec = tm.tm_sec;

		// update the websocket clients
		update_ws();
	}

	if(millis() % 3600000 == 0) {
		updateScheme_ws();
	}
}

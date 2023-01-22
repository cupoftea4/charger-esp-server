#include <esp-fs-webserver.h>  // https://github.com/cotestatnt/esp-fs-webserver
#include <FS.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>

#define FILESYSTEM LittleFS

#define ARDUINO_GET_ALL_DATA "a"
#define ALL_DATA_COUNT 7
#define ARDUINO_STOP_CHARGING "x"
#define ARDUINO_RESET "r"
#define ARDUINO_SET_CURRENT "i?"

#define ARDUINO_RESPONSE_TIMEOUT 3000
#define rxPin 13
#define txPin 12

ESP8266WebServer server(80);
FSWebServer myWebServer(FILESYSTEM, server);
SoftwareSerial arduinoSerial = SoftwareSerial(rxPin, txPin);

String readString(SoftwareSerial *const serial, unsigned long timeout = 50) {
  if (serial == NULL) return "";
  String input = "";
  char c;
  unsigned long timer = millis();
  while (millis() - timer < timeout) {
    if (serial->available()) {
      c = serial->read();
      if (c == '\n') break;
      if (c >= 0) input += c;
      timer = millis();
    }
  }
  return input;
}

String waitForResponse() {
  String input = "";
  unsigned long timer = millis();
  while (millis() - timer < ARDUINO_RESPONSE_TIMEOUT) {
    if (arduinoSerial.available()) {
      input = readString(&arduinoSerial);
      return input;
    }
  }
  arduinoSerial.flush();
  return input;
}

String getErrorMessage(int error) {
  switch (error) {
    case 1:
      return "Bad request";
    case 2:
      return "Wrong value";
    default:
      return "Unknown error";
  }
}

String requestArduino(const String command, bool shouldReturnZero = false) {
  arduinoSerial.println(command);
  Serial.println("Request: " + command);
  String response = waitForResponse();
  Serial.println("Response: " + response);
  if (response.isEmpty()) {
    myWebServer.webserver->send(408, "text/plain", "Arduino Timeout");
    return "";
  } 
  if (shouldReturnZero) {
    if (response.toInt() == 0) {
      myWebServer.webserver->send(200);
      return "";
    }
    const int error = response.toInt();
    myWebServer.webserver->send(500, "text/plain", getErrorMessage(error));
    return "";
  } 
  return response;
}

String getTextBatteryType(int type) {
  const String batteryTypes[] = {"None", "LiIon", "AGM"};
  if (type < 0 || type > 2) return "Unknown";
  return batteryTypes[type];
}

String getTextBatteryState(int state) {
  const String batteryStates[] = {"Charging", "Idle", "Full", "Error"};
  if (state < 0 || state > 3) return "Unknown";
  return batteryStates[state];
}

String parseJson(String data) {
  // ({current}, {voltage}, {needed current}, {percentage}, {pwm}, {type}, {state})
  String parsedData[ALL_DATA_COUNT]; 
  int i = 0, j = 0;
  for (const char c : data) {
    if (c == ',') {
      i++; j = 0;
      if (i > ALL_DATA_COUNT - 1) break;
    } else {
      parsedData[i] += c;
      j++;
    }
  }
  if (i != ALL_DATA_COUNT - 1) {
    Serial.println("ERROR: Wrong data format");
    myWebServer.webserver->send(418, "text/plain", "Got wrong data from Arduino");
    return "";
  }
  DynamicJsonDocument json(1024);
  json["current"] = parsedData[0];
  json["voltage"] = parsedData[1];
  json["target"] = parsedData[2];
  json["percent"] = parsedData[3];
  json["pwm"] = parsedData[4];
  json["type"] = getTextBatteryType(parsedData[5].toInt());
  json["state"] = getTextBatteryState(parsedData[6].toInt());
  String output;
  serializeJson(json, output);
  return output;
}


////////////////////////////////  Filesystem  /////////////////////////////////////////
void startFilesystem() {
  // FILESYSTEM INIT
  if ( FILESYSTEM.begin()){
    File root = FILESYSTEM.open("/", "r");
    File file = root.openNextFile();
    while (file){
      const char* fileName = file.name();
      size_t fileSize = file.size();
      Serial.printf("FS File: %s, size: %lu\n", fileName, (long unsigned)fileSize);
      file = root.openNextFile();
    }
    Serial.println();
  }
  else {
    Serial.println("ERROR on mounting filesystem. It will be formatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
}


////////////////////////////  HTTP Request Handlers  ////////////////////////////////////
void getAllData() {
  String response = requestArduino(ARDUINO_GET_ALL_DATA);
  if (response.isEmpty()) return;
  myWebServer.webserver->send(200, "application/json", parseJson(response));
}

void stopCharging() {
  requestArduino(ARDUINO_STOP_CHARGING, true);
}

void resetCharger() {
  requestArduino(ARDUINO_RESET, true);
}

void setCurrent() {
  if(myWebServer.webserver->hasArg("value")) {
    const auto value = myWebServer.webserver->arg("value").toInt();
    requestArduino(ARDUINO_SET_CURRENT + String(value), true);
  } else {
    myWebServer.webserver->send(400, "text/plain", "Bad Request");
  }
}


////////////////////////////////  Setup  /////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  arduinoSerial.begin(9600);

  // FILESYSTEM INIT
  startFilesystem();

  // Try to connect to flash stored SSID, start AP if fails after timeout
  IPAddress myIP = myWebServer.startWiFi(15000, "ESP8266_AP", "123456789" );
  myWebServer.webserver->enableCORS(true);

  // Add custom page handlers to webserver
  myWebServer.addHandler("/get_data", HTTP_GET, getAllData); // returns all data in JSON format
  myWebServer.addHandler("/stop_charging", HTTP_GET, stopCharging);
  myWebServer.addHandler("/reset", HTTP_GET, resetCharger);
  myWebServer.addHandler("/set_current", HTTP_GET, setCurrent);

  // Start webserver
  if (myWebServer.begin()) {
    Serial.print(F("ESP Web Server started on IP Address: "));
    Serial.println(myIP);
    Serial.println(F("Open /setup page to configure optional parameters"));
    Serial.println(F("Open /edit page to view and edit files"));
  }
}

void loop() {
  myWebServer.run();
}
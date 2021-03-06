/////////////////////////////////////////////////////////////////////////////
// waterelf.ino /////////////////////////////////////////////////////////////
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include "./DNSServer.h"      // Patched lib
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <FS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>
#include <RCSwitch.h>
#include "Adafruit_MCP23008.h"

/////////////////////////////////////////////////////////////////////////////
// resource management stuff ////////////////////////////////////////////////
int loopCounter = 0;
const int LOOP_ROLLOVER = 5000; // how many loops per action slice
const int TICK_MONITOR = 0;
const int TICK_WIFI_DEBUG = 500;
const int TICK_POST_DEBUG = 200;
const int TICK_HEAP_DEBUG = 1000;

/////////////////////////////////////////////////////////////////////////////
// wifi management stuff ////////////////////////////////////////////////////
const byte DNS_PORT = 53;
DNSServer dnsServer;
IPAddress apIP(192, 168, 99, 1);
IPAddress netMsk(255, 255, 255, 0);
ESP8266WebServer webServer(80);
String apSSIDStr = "WaterElf-" + String(ESP.getChipId());
const char* apSSID = apSSIDStr.c_str();
String svrAddr = ""; // address of a local server

/////////////////////////////////////////////////////////////////////////////
// MQTT stuff ///////////////////////////////////////////////////////////////
const boolean SEND_MQTT = true;  // turn on/off posting of data to couchdb
void callback(const MQTT::Publish& pub) {
  // handle message arrived
}

/////////////////////////////////////////////////////////////////////////////
// OTA update stuff /////////////////////////////////////////////////////////
// const uint16_t aport = 8266;
// WiFiServer TelnetServer(aport);
// WiFiClient Telnet;
// WiFiUDP OTA;

/////////////////////////////////////////////////////////////////////////////
// MCP23008 stuff ///////////////////////////////////////////////////////////
Adafruit_MCP23008 mcp;

/////////////////////////////////////////////////////////////////////////////
// page generation stuff ////////////////////////////////////////////////////
String pageTopStr = String(
  "<html><head><title>WaterElf Aquaponics Helper [ID: " + apSSIDStr + "]"
);
const char* pageTop = pageTopStr.c_str();
const char* pageTop2 = "</title>\n"
  "<meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
  "<style>body{padding: 20px;background: #FFF url(\"waterelf.jpg\") no-repeat;color: #000;font-family: sans-serif;font-size: 150%;}</style>"
  "</head><body>\n";
const char* pageDefault =
  "<h2>Welcome to WaterElf</h2>\n"
  "<h2>Control</h2>\n"
  "<p><ul>\n"
  "<li><a href='/wifi'>Join a wifi network</a></li>\n"
  "<li><a href='/serverconf'>Configure data sharing</a></li>\n"
  "<li>\n"
    "<form method='POST' action='actuate'>\n"
    "External power: "
    "on <input type='radio' name='state' value='on'>\n"
    "off <input type='radio' name='state' value='off' checked>\n"
    "<input type='submit' value='Submit'></form>\n"
  "</li>\n"
  "<li>\n"
    "<form method='POST' action='leftpump'>\n"
    "Left Water Pump: "
    "on <input type='radio' name='state' value='on'>\n"
    "off <input type='radio' name='state' value='off' checked>\n"
    "<input type='submit' value='Submit'></form>\n"
  "</li>\n"
  "<li>\n"
    "<form method='POST' action='rightpump'>\n"
    "Right Water Pump: "
    "on <input type='radio' name='state' value='on'>\n"
    "off <input type='radio' name='state' value='off' checked>\n"
    "<input type='submit' value='Submit'></form>\n"
  "</li>\n"
  "</ul></p>\n"
  "<h2>Monitor</h2>\n"
  "<p><ul>\n"
  "<li><a href='/wifistatus'>Wifi status</a></li>\n"
  "<li><a href='/data'>Sensor data</a></li>\n"
  "</ul></p>\n";
const char* pageFooter =
  "\n<p><a href='/'>WaterElf</a>&nbsp;&nbsp;&nbsp;"
  "<a href='https://now.wegrow.social/'>WeGrow</a></p></body></html>";

/////////////////////////////////////////////////////////////////////////////
// file serving web-pages ////////////////////////////////////////////////////
File fsUploadFile;
String getContentType(String filename){
  if(webServer.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  //Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = webServer.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(webServer.uri() != "/edit") return;
  HTTPUpload& upload = webServer.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(webServer.args() == 0) return webServer.send(500, "text/plain", "BAD ARGS");
  String path = webServer.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return webServer.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return webServer.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  webServer.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(webServer.args() == 0)
    return webServer.send(500, "text/plain", "BAD ARGS");
  String path = webServer.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return webServer.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return webServer.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return webServer.send(500, "text/plain", "CREATE FAILED");
  webServer.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!webServer.hasArg("dir")) {webServer.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = webServer.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  webServer.send(200, "text/json", output);
}

/////////////////////////////////////////////////////////////////////////////
// data monitoring stuff ////////////////////////////////////////////////////
const boolean SEND_COUCH = false;  // turn on/off posting of data to couchdb
const int MONITOR_POINTS = 60; // number of data points to store
struct monitor_t {
  unsigned long timestamp;
  long waterLevel;
  float waterCelsius;
  float airCelsius;
  float airHumid;
  uint16_t lux;
  float pH;
};
monitor_t monitorData[MONITOR_POINTS];
int monitorCursor = 0;
int monitorSize = 0;
const int DATA_ENTRIES = 30; // size of /data rpt; must be <= MONITOR_POINTS
void updateSensorData(monitor_t *monitorData);
void postSensorData(monitor_t *monitorData);
void sendSensorData(monitor_t *monitorData);
void printMonitorEntry(monitor_t m, String* buf);
void jsonMonitorEntry(monitor_t *m, String* buf);

/////////////////////////////////////////////////////////////////////////////
// level sensing stuff //////////////////////////////////////////////////////
const int levelTriggerPin=12;
const int levelEchoPin=16;
boolean GOT_LEVEL_SENSOR = false;  // we'll change later if we detect sensor

/////////////////////////////////////////////////////////////////////////////
// temperature sensor stuff /////////////////////////////////////////////////
OneWire ds(2); // DS1820 on pin 2 (a 4.7K resistor is necessary)
DallasTemperature tempSensor(&ds);  // pass through reference to library
void getTemperature(float* waterCelsius);
boolean GOT_TEMP_SENSOR = false; // we'll change later if we detect sensor
DeviceAddress tempAddr; // array to hold device address

/////////////////////////////////////////////////////////////////////////////
// humidity sensor stuff ////////////////////////////////////////////////////
DHT dht(0, DHT22); // what digital pin we're on, plus type DHT22 aka AM2302
boolean GOT_HUMID_SENSOR = false;  // we'll change later if we detect sensor

/////////////////////////////////////////////////////////////////////////////
// light sensor stuff ///////////////////////////////////////////////////////
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591); // sensor id
boolean GOT_LIGHT_SENSOR = false; // we'll change later if we detect sensor

/////////////////////////////////////////////////////////////////////////////
// pH sensor stuff //////////////////////////////////////////////////////////
byte pH_Add = 0; // loaded from SensorConfig.txt
int pH4Cal = 0,pH7Cal = 0; // loaded from SensorConfig.txt
float vRef = 0; // loaded from SensorConfig.txt
const float pHStep = 59.16; // ideal probe slope
const float opampGain = 5.25; //what is our Op-Amps gain (stage 1)
boolean GOT_PH_SENSOR = false; // we'll change later if we detect sensor

/////////////////////////////////////////////////////////////////////////////
// RC switch stuff //////////////////////////////////////////////////////////
RCSwitch mySwitch = RCSwitch();

/////////////////////////////////////////////////////////////////////////////
// config utils /////////////////////////////////////////////////////////////
boolean getCloudShare();
void setCloudShare(boolean b);
String getSvrAddr();
void setSvrAddr(String s);
void getSensorConfig(byte* pH_Add, int* pH4Cal, int* pH7Cal, float* vRef) {
  File f = SPIFFS.open("/SensorConfig.txt", "r");
  if(f) {
    *pH_Add=strtol(&f.readStringUntil('/')[0], NULL, 16);
    String comment1 = f.readStringUntil('\n');
    *pH4Cal=strtol(&f.readStringUntil('/')[0], NULL, 10);
    String comment2 = f.readStringUntil('\n');
    *pH7Cal=strtol(&f.readStringUntil('/')[0], NULL, 10);
    String comment3 = f.readStringUntil('\n');
    *vRef = f.readStringUntil('/').toFloat();
    String comment4 = f.readStringUntil('\n');
    f.close();
  }
}
/////////////////////////////////////////////////////////////////////////////
// misc utils ///////////////////////////////////////////////////////////////
void ledOn();
void ledOff();
String ip2str(IPAddress address);

/////////////////////////////////////////////////////////////////////////////
// setup ////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  Serial.println(); // start on new line
  pinMode(BUILTIN_LED, OUTPUT); // turn built-in LED on
  blink(3); // signal we're starting setup

  // read persistent config
  SPIFFS.begin();
  svrAddr = getSvrAddr();
  getSensorConfig(&pH_Add,&pH4Cal,&pH7Cal,&vRef);

  startPeripherals();
  startAP();
  printIPs();
  startDNS();
  startWebServer();
  
//  MDNS.begin(apSSID);
//  MDNS.addService("arduino", "tcp", aport);
//  OTA.begin(aport);
//  TelnetServer.begin();
//  TelnetServer.setNoDelay(true);
  mcp.begin();      // use default address 0 for mcp23008
  mcp.pinMode(0, OUTPUT);
  mcp.pinMode(5, OUTPUT);
  mcp.pinMode(6, OUTPUT);    
  if(WiFi.hostname("waterelf"))
    Serial.println("set hostname succeeded");
  else
    Serial.println("set hostname failed");

  delay(300); blink(3); // signal we've finished config
}

/////////////////////////////////////////////////////////////////////////////
// looooooooooooooooooooop //////////////////////////////////////////////////
void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
/* if (OTA.parsePacket()) {
    IPAddress remote = OTA.remoteIP();
   int cmd  = OTA.parseInt();
    int port = OTA.parseInt();
    int size   = OTA.parseInt();

    Serial.print("Update Start: ip:");
    Serial.print(remote);
    Serial.printf(", port:%d, size:%d\n", port, size);
    uint32_t startTime = millis();

    WiFiUDP::stopAll();

    if(!Update.begin(size)){
      Serial.println("Update Begin Error");
      return;
    }

    WiFiClient client;
    if (client.connect(remote, port)) {

      uint32_t written;
      while(!Update.isFinished()){
        written = Update.write(client);
        if(written > 0) client.print(written, DEC);
      }
      Serial.setDebugOutput(false);

      if(Update.end()){
        client.println("OK");
        Serial.printf("Update Success: %u\nRebooting...\n", millis() - startTime);
        ESP.restart();
      } else {
        Update.printError(client);
        Update.printError(Serial);
      }
    } else {
      Serial.printf("Connect Failed: %u\n", millis() - startTime);
    }
  }
  //IDE Monitor (connected to Serial)
  if (TelnetServer.hasClient()){
    if (!Telnet || !Telnet.connected()){
      if(Telnet) Telnet.stop();
      Telnet = TelnetServer.available();
    } else {
      WiFiClient toKill = TelnetServer.available();
      toKill.stop();
    }
  }
  if (Telnet && Telnet.connected() && Telnet.available()){
    while(Telnet.available())
      Serial.write(Telnet.read());
  }
  if(Serial.available()){
    size_t len = Serial.available();
    uint8_t * sbuf = (uint8_t *)malloc(len);
    Serial.readBytes(sbuf, len);
    if (Telnet && Telnet.connected()){
      Telnet.write((uint8_t *)sbuf, len);
      yield();
    }
    free(sbuf);
  }
  */
  
  delay(1);
  if(loopCounter == TICK_MONITOR) {
    updateSensorData(monitorData);
    delay(5); // TODO a better way?!
  } 
  if(loopCounter == TICK_WIFI_DEBUG) {
//    Serial.print("SSID: "); Serial.print(apSSID);
//    Serial.print("; IP address(es): local="); Serial.print(WiFi.localIP());
//    Serial.print("; AP="); Serial.println(WiFi.softAPIP());
  }
  if(loopCounter == TICK_HEAP_DEBUG) {
//    Serial.print("free heap="); Serial.println(ESP.getFreeHeap());
  }

  if(loopCounter++ == LOOP_ROLLOVER) loopCounter = 0;
}

/////////////////////////////////////////////////////////////////////////////
// wifi and web server management stuff /////////////////////////////////////
void startAP() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(apSSID);
  Serial.println("Soft AP started");
}
void startDNS() {
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS server started");
}
void printIPs() {
  Serial.print("AP SSID: ");
  Serial.print(apSSID);
  Serial.print("; IP address(es): local=");
  Serial.print(WiFi.localIP());
  Serial.print("; AP=");
  Serial.println(WiFi.softAPIP());
}
void startWebServer() {
  webServer.on("/", handle_root);
  webServer.on("/generate_204", handle_root); // Android support
  webServer.on("/L0", handle_root);
  webServer.on("/L2", handle_root);
  webServer.on("/ALL", handle_root);
  webServer.onNotFound(handleNotFound);
  webServer.on("/list", HTTP_GET, handleFileList);
  webServer.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) webServer.send(404, "text/plain", "FileNotFound");
  });
  //create file
  webServer.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  webServer.on("/edit", HTTP_DELETE, handleFileDelete);
  //called after file upload
  webServer.on("/edit", HTTP_POST, [](){ webServer.send(200, "text/plain", ""); });
  //called when a file is received inside POST data
  webServer.onFileUpload(handleFileUpload);

  webServer.on("/wifi", handle_wifi);
  webServer.on("/wifistatus", handle_wifistatus);
  webServer.on("/serverconf", handle_serverconf);
  webServer.on("/wfchz", handle_wfchz);
  webServer.on("/svrchz", handle_svrchz);
  webServer.on("/data", handle_data);
  webServer.on("/actuate", handle_actuate);
  webServer.on("/leftpump", handle_leftpump);
  webServer.on("/rightpump", handle_rightpump);  
  webServer.begin();
  Serial.println("HTTP server started");
}
void handleNotFound() {
  // This loads from SPIFFS if URL isn't defined above
  if(!handleFileRead(webServer.uri())){
    webServer.send(404, "text/plain", "FileNotFound");
    Serial.print("File Not Found: ");
    } else {
      Serial.print("Served from SPIFFS: ");
    }
  Serial.println(webServer.uri());
  // TODO send redirect to /? or just use handle_root?
}
void handle_root() {
  Serial.println("serving page notionally at /");
  String toSend = pageTop;
  toSend += pageTop2;
  toSend += pageDefault;
  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}
void handle_data() {
  Serial.println("serving page at /data");
  String toSend = pageTop;
  toSend += ": Sensor Data";
  toSend += pageTop2;
  toSend += "\n<h2>Sensor Data</h2><p><pre>\n";

  int mSize = monitorSize;
  for(
    int i = monitorCursor - 1, j = 1;
    j <= DATA_ENTRIES && j <= monitorSize;
    i--, j++
  ) {
    jsonMonitorEntry(&monitorData[i], &toSend);
    toSend += "\n";
    if(i == 0)
      i = MONITOR_POINTS;
  }

  toSend += "</pre>\n";
  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}
String genAPForm() {
  String f = pageTop;
  f += ": Wifi Config";
  f += pageTop2;
  f += "<h2>Choose a wifi access point to join</h2>\n";
  f += "<h3>Signal strength in brackets, lower is better</h3><p>\n";
  
  const char *checked = " checked";

  int n = WiFi.scanNetworks();
  Serial.print("scan done: ");
  if(n == 0) {
    Serial.println("no networks found");
    f += "No wifi access points found :-( ";
    f += "<a href='/'>Back</a><br/><a href='/wifi'>Try again?</a></p>\n";
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    f += "<form method='POST' action='wfchz'> ";
    for(int i = 0; i < n; ++i) {
      // print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");

      f.concat("<input type='radio' name='ssid' value='");
      f.concat(WiFi.SSID(i));
      f.concat("'");
      f.concat(checked);
      f.concat(">");
      f.concat(WiFi.SSID(i));
      f.concat(" (");
      f.concat(WiFi.RSSI(i));
      f.concat(" dBm)");
      f.concat("<br/>\n");
      checked = "";
    }
    f += "<br/>Pass key: <input type='textarea' name='key'><br/><br/> ";
    f += "<input type='submit' value='Submit'></form></p>";
  }

  f += pageFooter;
  return f;
}
void handle_wifi() {
  Serial.println("serving page at /wifi");
  String toSend = genAPForm();
  webServer.send(200, "text/html", toSend);
}

void handle_wifistatus() {
  Serial.println("serving page at /wifistatus");

  String toSend = pageTop;
  toSend += ": Wifi Status";
  toSend += pageTop2;
  toSend += "\n<h2>Wifi Status</h2><p><ul>\n";

  toSend += "\n<li>SSID: ";
  toSend += WiFi.SSID();
  toSend += "</li>";
  toSend += "\n<li>Status: ";
  switch(WiFi.status()) {
    case WL_IDLE_STATUS:
      toSend += "WL_IDLE_STATUS</li>"; break;
    case WL_NO_SSID_AVAIL:
      toSend += "WL_NO_SSID_AVAIL</li>"; break;
    case WL_SCAN_COMPLETED:
      toSend += "WL_SCAN_COMPLETED</li>"; break;
    case WL_CONNECTED:
      toSend += "WL_CONNECTED</li>"; break;
    case WL_CONNECT_FAILED:
      toSend += "WL_CONNECT_FAILED</li>"; break;
    case WL_CONNECTION_LOST:
      toSend += "WL_CONNECTION_LOST</li>"; break;
    case WL_DISCONNECTED:
      toSend += "WL_DISCONNECTED</li>"; break;
    default:
       toSend += "unknown</li>";
  }

  toSend += "\n<li>Local IP: ";   toSend += ip2str(WiFi.localIP());
  toSend += "</li>\n";
  toSend += "\n<li>Soft AP IP: "; toSend += ip2str(WiFi.softAPIP());
  toSend += "</li>\n";
  toSend += "\n<li>AP SSID name: "; toSend += apSSID;
  toSend += "</li>\n";

  toSend += "</ul></p>";

  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}
void handle_wfchz() {
  Serial.println("serving page at /wfchz");
  String toSend = pageTop;
  toSend += ": joining wifi network";
  toSend += pageTop2;
  String ssid = "";
  String key = "";

  for(uint8_t i = 0; i < webServer.args(); i++ ) {
    //Serial.println(" " + webServer.argName(i) + ": " + webServer.arg(i));
    if(webServer.argName(i) == "ssid")
      ssid = webServer.arg(i);
    else if(webServer.argName(i) == "key")
      key = webServer.arg(i);
  }

  if(ssid == "") {
    toSend += "<h2>Ooops, no SSID...?</h2>";
    toSend += "<p>Looks like a bug :-(</p>";
  } else {
    toSend += "<h2>Done! Now trying to join network...</h2>";
    toSend += "<p>Check <a href='/wifistatus'>wifi status here</a>.</p>";
    ssid.replace("+", " ");
    char ssidchars[ssid.length()+1];
    char keychars[key.length()+1];
    ssid.toCharArray(ssidchars, ssid.length()+1);
  if(ssidchars == "") {
    toSend += "<h2>Ooops, no SSID...?</h2>";
    toSend += "<p>Looks like a bug :-(</p>";
  }
    key.toCharArray(keychars, key.length()+1);
    Serial.print("ssid: ");
    Serial.println(ssid);
    Serial.print("key: ");
    Serial.println(key);
    Serial.print("ssidshars: ");
    Serial.println(ssidchars);
    Serial.print("keyshars: ");
    Serial.println(keychars);
    WiFi.begin(ssidchars, keychars);
  }

  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}
String genServerConfForm() {
  String f = pageTop;
  f += ": Server Config";
  f += pageTop2;
  f += "<h2>Configure data sharing</h2><p>\n";

  f += "<form method='POST' action='svrchz'> ";
  f += "<br/>Local server IP address: ";
  f += "<input type='textarea' name='svraddr'><br/><br/> ";
  //f += "Sharing on WeGrow.social: ";
  // TODO set checked dependent on getCloudShare()
  //f += "on <input type='radio' name='wegrow' value='on' checked>\n";
  //f += "off <input type='radio' name='wegrow' value='off'><br/><br/>\n";
  f += "<input type='submit' value='Submit'></form></p>";

  f += pageFooter;
  return f;
}
void handle_serverconf() {
  Serial.println("serving page at /serverconf");
  String toSend = genServerConfForm();
  webServer.send(200, "text/html", toSend);
}
void handle_svrchz() {
  Serial.println("serving page at /svrchz");
  String toSend = pageTop;
  toSend += ": data sharing configured";
  toSend += pageTop2;

  boolean cloudShare = false;
  for(uint8_t i = 0; i < webServer.args(); i++) {
    if(webServer.argName(i) == "svraddr") {
      svrAddr = webServer.arg(i);
      toSend += "<h2>Added local server config...</h2>";
      toSend += "<p>...at ";
      toSend += svrAddr;
      toSend += "</p>";
    } else if(webServer.argName(i) == "key") {
      if(webServer.arg(i) == "on")
        cloudShare = true;
    }
  }

  // persist the config
  if(svrAddr.length() > 0) setSvrAddr(svrAddr);
  setCloudShare(cloudShare);

  // TODO some way of verifying if server config worked
  // add srvstatus, or roll that into wifistatus, or...?

  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}
void handle_actuate() {
  Serial.println("serving page at /actuate");
  String toSend = pageTop;
  toSend += ": Setting Actuator";
  toSend += pageTop2;

  boolean newState = false;
  for(uint8_t i = 0; i < webServer.args(); i++ ) {
    if(webServer.argName(i) == "state") {
      if(webServer.arg(i) == "on")
        newState = true;
    }
  }

  // now we trigger the 433 transmitter
  if(newState == true){
    mySwitch.switchOn(4, 2);
    Serial.println("Actuator on");
  } else {
    mySwitch.switchOff(4, 2);
    Serial.println("Actuator off");
  }

  toSend += "<h2>Actuator triggered</h2>\n";
  toSend += "<p>(New state is ";
  toSend += (newState) ? "on" : "off";
  toSend += ".)</p>\n";
  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}

void handle_leftpump() {
  Serial.println("serving page at /leftpump");
  String toSend = pageTop;
  toSend += ": Setting Left Water Pump";
  toSend += pageTop2;

  boolean newState = false;
  for(uint8_t i = 0; i < webServer.args(); i++ ) {
    if(webServer.argName(i) == "state") {
      if(webServer.arg(i) == "on")
        newState = true;
    }
  }

  // now we trigger the mcp23008 to turn MOSFETs off or on
  if(newState == true){
    mcp.digitalWrite(0, HIGH);
    Serial.println("Left Water Pump on");
  } else {
    mcp.digitalWrite(0, LOW);
    Serial.println("Left Water Pump off");
  }

  toSend += "<h2>Left Water Pump triggered</h2>\n";
  toSend += "<p>(New state is ";
  toSend += (newState) ? "on" : "off";
  toSend += ".)</p>\n";
  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}

void handle_rightpump() {
  Serial.println("serving page at /rightpump");
  String toSend = pageTop;
  toSend += ": Setting Right Water Pump";
  toSend += pageTop2;

  boolean newState = false;
  for(uint8_t i = 0; i < webServer.args(); i++ ) {
    if(webServer.argName(i) == "state") {
      if(webServer.arg(i) == "on")
        newState = true;
    }
  }

  // now we trigger the mcp23008 to turn MOSFETs off or on
  if(newState == true){
    mcp.digitalWrite(5, HIGH);
    Serial.println("Right Water Pump on");
  } else {
    mcp.digitalWrite(5, LOW);
    Serial.println("Right Water Pump off");
  }

  toSend += "<h2>Right Water Pump triggered</h2>\n";
  toSend += "<p>(New state is ";
  toSend += (newState) ? "on" : "off";
  toSend += ".)</p>\n";
  toSend += pageFooter;
  webServer.send(200, "text/html", toSend);
}

/////////////////////////////////////////////////////////////////////////////
// sensor/actuator stuff ////////////////////////////////////////////////////
void startPeripherals() {
  Serial.println("startPeripherals");
  pinMode(levelTriggerPin, OUTPUT);
  pinMode(levelEchoPin, INPUT);
  GOT_LEVEL_SENSOR = true;

  mySwitch.enableTransmit(15);   // RC transmitter is connected to Pin 15

  tempSensor.begin();     // start the onewire temperature sensor
  if(tempSensor.getDeviceCount()==1) {
    GOT_TEMP_SENSOR = true;
    Serial.println("Found waterproof temperature sensor");
    tempSensor.getAddress(tempAddr, 0);
    tempSensor.setResolution(tempAddr, 12); // 12 bit res (DS18B20 does 9-12)
  }
  
  dht.begin();    // Start the humidity and air temperature sensor
  float airHumid = dht.readHumidity();
  float airCelsius = dht.readTemperature();
  if (isnan(airHumid) || isnan(airCelsius)) {
    Serial.println("Failed to find humidity sensor");
  } else {
    Serial.println("Found humidity sensor");
    GOT_HUMID_SENSOR = true;
  }

  Wire.begin(4,5);
  byte error;
  Wire.beginTransmission(0x29);
  error = Wire.endTransmission();
  if(error==0){
    GOT_LIGHT_SENSOR = true;
    Serial.println("Found light sensor");
    tsl.begin();  // startup light sensor
    // can change gain of light sensor on the fly, to adapt 
    // brighter/dimmer light situations
    tsl.setGain(TSL2591_GAIN_LOW);    // 1x gain (bright light)
    // tsl.setGain(TSL2591_GAIN_MED);       // 25x gain
    // tsl.setGain(TSL2591_GAIN_HIGH);   // 428x gain
  
    // changing the integration time gives you a longer time over which to
    // sense light longer timelines are slower, but are good in very low light
    // situtations!
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS); // shortest (bright)
    // tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
    // tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
    // tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
    // tsl.setTiming(TSL2591_INTEGRATIONTIME_500MS);
    // tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS); // longest (dim)
  }
  
  Wire.beginTransmission(pH_Add);
  error = Wire.endTransmission();
  if(error==0){
    GOT_PH_SENSOR = true;
    Serial.println("Found pH sensor");
  }
}
void updateSensorData(monitor_t *monitorData) {
  // Serial.print("monitorCursor = "); Serial.print(monitorCursor);
  // Serial.print(" monitorSize = ");  Serial.println(monitorSize);

  monitor_t* now = &monitorData[monitorCursor];
  if(monitorSize < MONITOR_POINTS)
    monitorSize++;
  now->timestamp = millis();
  if(GOT_LEVEL_SENSOR)
    getLevel(&(now->waterLevel));

  if(GOT_TEMP_SENSOR)
    getTemperature(&(now->waterCelsius));
    
  if(GOT_HUMID_SENSOR)
    getHumidity(&now->airCelsius, &now->airHumid);
    
  if(GOT_LIGHT_SENSOR)
    getLight(&now->lux);

  if(GOT_PH_SENSOR)
    getPH(&now->pH);
    
  if(SEND_COUCH) postSensorData(&monitorData[monitorCursor]);
  if(SEND_MQTT) sendSensorData(&monitorData[monitorCursor]);
    
  if(++monitorCursor == MONITOR_POINTS)
    monitorCursor = 0;
}
void postSensorData(monitor_t *monitorData) {
  //Serial.println("\npostSensorData");

  // create a JSON form
  String jsonBuf = "";
  jsonMonitorEntry(monitorData, &jsonBuf);
  String envelope = "POST /fishydata HTTP/1.1\n";
  envelope += "Content-Type: application/json\n";
  envelope += "Content-Length: " ;
  envelope += jsonBuf.length();
  envelope += "\nConnection: close\n\n";
  envelope += jsonBuf;
  //Serial.println(envelope);
  
  WiFiClient couchClient;
  if(couchClient.connect(svrAddr.c_str(), 5984)) {
    Serial.print(svrAddr);
    Serial.println(" - connected as couch server");
    couchClient.print(envelope);
  } else {
    Serial.print(svrAddr);
    Serial.println(" - no couch server found!");
  }

  Serial.println("");
  return;
}
void sendSensorData(monitor_t *monitorData) {
  Serial.println("\nsendSensorData");
  WiFiClient wclient;
  PubSubClient client(wclient, svrAddr);
  client.set_callback(callback); // Register MQTT callback
  if (client.connect("arduinoClient")) {
    if(GOT_LEVEL_SENSOR){
      String lv;
      lv.concat(monitorData->waterLevel);
      client.publish("WaterLevel",lv);
    }
    if(GOT_TEMP_SENSOR){
      String wc;
      wc.concat(monitorData->waterCelsius);
      client.publish("WaterTemp",wc);
    }
    if(GOT_HUMID_SENSOR){
      String ac,hm;
      ac.concat(monitorData->airCelsius);
      hm.concat(monitorData->airHumid);
      client.publish("AirTemp",ac);
      client.publish("Humidity",hm);
    }  
    if(GOT_LIGHT_SENSOR){
      String lx;
      lx.concat(monitorData->lux);
      client.publish("Light",lx);
    }
    if(GOT_PH_SENSOR){
      String ph;
      ph.concat(monitorData->pH);
      client.publish("pH",ph);
    }
  }
  return;
}

void jsonMonitorEntry(monitor_t *m, String* buf) {
  buf->concat("{ ");
  buf->concat("\"timestamp\": ");
  buf->concat(m->timestamp);
  if(GOT_LEVEL_SENSOR){
    buf->concat(", \"waterLevel\": ");
    buf->concat(m->waterLevel);
  }
  if(GOT_TEMP_SENSOR){
    buf->concat(", \"waterTemp\": ");
    buf->concat(m->waterCelsius);
  }
  if(GOT_HUMID_SENSOR){  
    buf->concat(", \"airTemp\": ");
    buf->concat(m->airCelsius);
    buf->concat(", \"humidity\": ");
    buf->concat(m->airHumid);
  }
  if(GOT_LIGHT_SENSOR){
    buf->concat(", \"lux\": ");
    buf->concat(m->lux);
  }
  if(GOT_PH_SENSOR){
    buf->concat(", \"pH\": ");
    buf->concat(m->pH);
  }
  buf->concat(" }");
}
void getLevel(long* waterLevel) {
  long duration;
  digitalWrite(levelTriggerPin, LOW);  // prepare for ping
  delayMicroseconds(2);
  digitalWrite(levelTriggerPin, HIGH); // start ping
  delayMicroseconds(10); // Allow 10ms ping
  digitalWrite(levelTriggerPin, LOW);  // stop ping
  duration = pulseIn(levelEchoPin, HIGH); //wait for response
  (*waterLevel) = (duration/2) / 29.1;
  Serial.print("Water Level: ");
  if ((*waterLevel) >= 200 || (*waterLevel) <= 0){
    Serial.println("is out of range!");
  }
  else {
    Serial.print(*waterLevel);
    Serial.println(" cm, ");
  }

  return;
}
void getTemperature(float* waterCelsius) {
  tempSensor.requestTemperatures(); // send command to get temperatures
  (*waterCelsius) = tempSensor.getTempC(tempAddr);
  Serial.print("Water Temp: ");
  Serial.print(*waterCelsius);
  Serial.println(" C, ");
  return;
}
void getHumidity(float* airCelsius, float* airHumid) {
  (*airCelsius) = dht.readTemperature();
  (*airHumid) = dht.readHumidity();
  Serial.print("Air Temp: ");
  Serial.print(*airCelsius);
  Serial.print(" C, ");
  Serial.print("Humidity: ");
  Serial.print(*airHumid);
  Serial.println(" %RH, ");
  return;
}
void getLight(uint16_t* lux) {
  sensors_event_t event;
  tsl.getEvent(&event);
  (*lux) = event.light; 
  Serial.print("Light: ");
  Serial.print(*lux);
  Serial.println(" Lux");
  return;
}
void getPH(float* pH) {
  // this is our I2C ADC interface section
  // assign 2 BYTES variables to capture the LSB & MSB (or Hi Low in this case)
  byte adc_high;
  byte adc_low;
  // we'll assemble the 2 in this variable
  int adc_result;
  
  Wire.requestFrom(pH_Add, 2); // requests 2 bytes
  while(Wire.available() < 2); // while two bytes to receive
  adc_high = Wire.read();      // set...
  adc_low = Wire.read();       // ...them
  // now assemble them, remembering byte maths; a Union works well here too
  adc_result = (adc_high * 256) + adc_low;
  Serial.print("Raw result: ");
  Serial.println(adc_result);
  // we have a our Raw pH reading from the ADC; now figure out what the pH is  
  float miliVolts = (((float)adc_result/4096)*vRef)*1000;
  float temp = ((((vRef*(float)pH7Cal)/4096)*1000)- miliVolts)/opampGain;
  (*pH) = 7-(temp/pHStep); 
  Serial.print("pH: ");
  Serial.println(*pH);
  return;
}

/////////////////////////////////////////////////////////////////////////////
// config utils /////////////////////////////////////////////////////////////
boolean getCloudShare() {
  boolean b = false;
  File f = SPIFFS.open("/cloudShare.txt", "r");
  if(f) {
    b = true;
    f.close();
  }
  return b;
}
void setCloudShare(boolean b) {
  if(b) {
    File f = SPIFFS.open("/cloudShare.txt", "w");
    f.println("");
    f.close();
  } else {
    SPIFFS.remove("/cloudShare.txt");
  }
}
String getSvrAddr() {
  String s = "";
  File f = SPIFFS.open("/svrAddr.txt", "r");
  if(f) {
    s = f.readString();
    s.trim();
    f.close();
  }
  return s;
}
void setSvrAddr(String s) {
  File f = SPIFFS.open("/svrAddr.txt", "w");
  f.println(s);
  f.close();
}

/////////////////////////////////////////////////////////////////////////////
// misc utils ///////////////////////////////////////////////////////////////
void ledOn() { digitalWrite(BUILTIN_LED, LOW); }
void ledOff() { digitalWrite(BUILTIN_LED, HIGH); }
void blink(int times) {
  ledOff();
  for(int i=0; i<times; i++) {
    ledOn(); delay(300); ledOff(); delay(300);
  }
  ledOff();
}
String ip2str(IPAddress address) {
  return
    String(address[0]) + "." + String(address[1]) + "." + 
    String(address[2]) + "." + String(address[3]);
}

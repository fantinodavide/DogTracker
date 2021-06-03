#include <TinyGPS++.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <axp20x.h>
#include <HardwareSerial.h>
#include "externalProcedures.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <LoRa.h>
#include <WebSocketsServer.h>
//#include <LinkedList.h>

#define SDA 21
#define SCL 22
#define csPin 18
#define resetPin 23
#define irqPin 38
#define interval 500
#define irq_pin 35
#define buz_pin 13

int defaultMinsInterval = 7;

HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

const char* ssid = "DogTracker";
const char* password = "password";

AsyncWebServer server(80);

AXP20X_Class axp;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

byte localAddress = 0xFF;
byte destination = 0xBB;
//byte localAddress = 0xBB;
//byte destination = 0xFF;
long lastSendTime = 0;
long lastGpsUpd = 0;
long msgSent = 0;
bool waitingAck = false;
long ackMsgId = -1;
String expectedAck = "";
long maxWaitStartTime = 0;
int lastCollareSender;
String lastCollareGpsVal;
long lastCollareMillis = 0;
int lastCollareRssi = 0;
bool buzTimeoutForced = false;
long lastOledOn = 0;
bool displayMinContr = false;
bool pmu_irq = false;
bool funcBtnPressed = false;
bool buzSound = false;
long lastBuzSound = 0;
long buzInterval = 300;
bool buzState = false;
long lastPositionRequest;

WebSocketsServer webSocket(81);

class coord{
  public:
    double lng;
    double lat;
};
/*class collare{
  public:
    int address;
    coord *lastPos;
    long recMillis;
    
    collare(int addr){
      lastPos = new coord();
      this->address = addr;
    }
    
    void setLastPos(double lat, double lng){
      lastPos->lat = lat;
      lastPos->lng = lng;
      recMillis = millis();
    }
    coord getLastPos(){
      lastPos;
    }
};*/

//LinkedList<collare*> collari = LinkedList<collare*>();

void setup() {
  Serial.begin(115200);
  beginWire(10);
  beginAxp(10);
  beginDisplay();
  
  axp.setChargingTargetVoltage(AXP202_TARGET_VOL_4_2V);
  //axp.setChargingTargetVoltage(AXP202_TARGET_VOL_4_36V);
  axp.setChargeControlCur(AXP1XX_CHARGE_CUR_550MA);
  axp.setShutdownTime(AXP_POWER_OFF_TIME_4S);
  axp.setlongPressTime(AXP_LONGPRESS_TIME_1S);

  powerDevice("OLED", "ON");
  powerDevice("GPS", "ON");
  powerDevice("LORA", "ON");
  powerDevice("DC1", "OFF");

  pinMode(irq_pin, INPUT);
  pinMode(38, INPUT);
  pinMode(buz_pin,OUTPUT);
  attachInterrupt(irq_pin, []{
    pmu_irq = true;  
  }, FALLING);
  axp.clearIRQ();
  axp.enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);

  attachInterrupt(digitalPinToInterrupt(38), []{
    funcBtnPressed = true;  
  }, FALLING);

  ledcSetup(0,1E5,12);
  ledcAttachPin(25,0);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  WiFi.mode(WIFI_AP);
  //WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  bool res = WiFi.softAP(ssid);

  server.onNotFound(handleNotFound);

  server.begin();
  startWebSocket();
  
  beginLora();
  LoRa.setTxPower(20);
  
  gpsSerial.begin(9600, SERIAL_8N1, 34, 12);
  
  writeOled(0, "DogTracker v1.0");
  displayBasePrint();
  
  setBuzzer(true);
  delay(50);
  setBuzzer(false);

  sendPerimeterCoords();
  loraSendMessage(String("conf"), String("sendPos"), String(++msgSent),true);
}

void loop() {
  cliProcess();
  loraOnReceive(LoRa.parsePacket());
  webSocket.loop();

  //Serial.println("Charging done IRQ: " + String(axp.isChargingDoneIRQ()?"TRUE":"FALSE"));
  //Serial.println("Chargeing: " + String(axp.isChargeing()?"TRUE":"FALSE"));
  //Serial.println("Charging IRQ: " + String(axp.isChargingIRQ()?"TRUE":"FALSE"));
  //Serial.println();
  if (axp.isChargeing() && !axp.isChargingDoneIRQ()) {
    axpLed("ON");
  } else {
    axpLed("OFF");
  }

  if(buzSound && millis() > lastBuzSound + buzInterval){
    if(!buzState){
      setBuzzer(true);
      //axpLed("ON");
    }else{
      setBuzzer(false);
      //axpLed("OFF");
    }
    buzState = !buzState;
    lastBuzSound = millis();
  }else if(!buzSound){
    setBuzzer(false);
  }

  if(millis() > lastCollareMillis + ((defaultMinsInterval + 0.25)*60*1000)){
    if(millis() > lastPositionRequest + 2000){
      loraSendMessage(String("conf"), String("sendPos"), String(++msgSent),false);
      lastPositionRequest = millis();
    }
    if(millis() > lastCollareMillis + ((defaultMinsInterval + 1)*60*1000)){
      buzSound = true;
      buzTimeoutForced = true;  
      //clearOledLine(3);
      //clearOledLine(4);
      writeOled(3, "Collare ");
      writeOled(4, "  Connessione persa");
    }else if(buzTimeoutForced){
      buzSound = false;
      buzTimeoutForced = false;
    }
  }
  
  if ( pmu_irq) {
    pmu_irq = false;
    /*axp.readIRQ();
    if (axp.isPEKShortPressIRQ()) {*/
    axp.clearIRQ();
    pwrBtnShortPress();
    //}
  }
  if(funcBtnPressed){
    funcBtnPressed = false;
    //buzSound = !buzSound;
    buzSound = false;
  }

  if(millis()-lastOledOn > 15000 && millis()-lastOledOn < 20000 && !displayMinContr){
    for(int i=128;i>0;i--){
      displaySetContrast(i);
      delay(1);
    }
  }else if(millis()-lastOledOn >= 20000){
    powerDevice("OLED","OFF");
  }

  if (millis() - lastSendTime > interval) {
    lastSendTime = millis();            // timestamp the message

    displayBasePrint();
    //writeOled(3, "Cercatori: " + String(webSocket.connectedClients(true)));
    
    websocketSendGps(lastCollareSender, lastCollareGpsVal);
    
    if (webSocket.connectedClients(true) > 0/* && gps.location.lat()!=0 && gps.location.lng()!=0*/) {
      gpsUpdate(1);
      if(gps.location.lng()!=0 && gps.location.lat()!=0 && gps.satellites.value() > 4)
        webSocket.broadcastTXT(String("{\"sender\":\"cercatore\"") + String(",\"type\":\"gps\",\"val\":\"") + (String(gps.location.lat(), 7) + "-" + String(gps.location.lng(), 7)) + String("\"}"));
      if(lastCollareSender && lastCollareGpsVal)
        websocketSendGps(lastCollareSender, lastCollareGpsVal);
    }
  }
}

void displayBasePrint(){
  float vbat = axp.getBattVoltage() / 1000;
  for(int i=100;i>0 && vbat<0;i--){
      vbat = axp.getBattVoltage()/1000;
      delay(250);
  }
  if(vbat>0){
    int batPerc = calcBatteryPerc(axp.getBattVoltage());
    String moreTxt = "";
    if(axp.isChargeing() && !axp.isChargingDoneIRQ()) moreTxt = "CAR";
    
    writeOled(1, "Batt. Telec.: " + String(batPerc) + "% " + moreTxt);// - " + String(vbat) + " v ");
    
    if(vbat<=3.3)
      writeOled(2, "Caricare Telec.");
    else if(batPerc >= 97)
      writeOled(2, "Carica completata.");
    else
      clearOledLine(2);
  }
}
int calcBatteryPerc(float vbat){
  return map(vbat, 3100, 4200, 0, 100);
}
bool setBuzzer(bool enabled){
  if(enabled){
    digitalWrite(buz_pin,HIGH);
  }else{
    digitalWrite(buz_pin,LOW);
  }
}
void pwrBtnShortPress(){
  Serial.println("btnShortPress");
  powerDevice("OLED","ON");
}
void gpsUpdate(int secBetweenUpd) {
  if (millis() > lastGpsUpd + (secBetweenUpd * 1000)) {
    while (gpsSerial.available()) {
      gps.encode(gpsSerial.read());
    }
  }
}
boolean beginWire(int tests) {
  cleari2c(SCL, SDA);
  boolean wireRunning = Wire.begin(21, 22);
  if (wireRunning) {
    Serial.println("Wire Begin PASS");
  } else if (tests > 0) {
    if (tests == 10)
      Serial.print("Wire Begin FAIL");
    else
      Serial.print(".");
    delay(5000);
    cleari2c(SCL, SDA);
    wireRunning = beginWire(--tests);
  } else {
    Serial.println("\nREQUIRED MANUAL REBOOT");
    while (true) {}
  }
  return wireRunning;
}
boolean beginAxp(int tests) {
  boolean isRunning = !axp.begin(Wire, AXP192_SLAVE_ADDRESS);
  if (isRunning) {
    Serial.println("AXP Begin PASS");
  } else if (tests > 0) {
    if (tests == 10)
      Serial.print("AXP Begin FAIL");
    else
      Serial.print(".");
    isRunning = beginAxp(--tests);
  }
  return isRunning;
}
boolean beginDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.invertDisplay(false);
  display.clearDisplay();
}
void beginLora() {
  LoRa.setPins(csPin, resetPin, irqPin);
  if (!LoRa.begin(866E6)) {
    Serial.println("LoRa init failed. Check your connections.");
    while (true);
  }
  Serial.println("LoRa init succeeded.");
}
/*bool isCollareKnown(int chAddr){
  bool output = false;
  for(int i=0;i<collari.size() && !output;i++){
    if(collari.get(i)->address == chAddr)
      output = true;
  }
  return output;
}*/

void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");
}
void axpLed(String power) {
  if (power == "ON")
    axp.setChgLEDMode(AXP20X_LED_LOW_LEVEL);
  else
    axp.setChgLEDMode(AXP20X_LED_OFF);
}
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        websocketSendGps(lastCollareSender, lastCollareGpsVal);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        //writeOled(3, "Cercatori: " + String(webSocket.connectedClients(true)));
        loraSendMessage(String("conf"), String("sendPos"), String(++msgSent),false);
      }
      break;
    case WStype_TEXT:
      String pload = String((char*)payload);
      if (pload.startsWith("pCoords:")) {
        File file = SPIFFS.open("/pCoords.json", FILE_WRITE);
        if (file) {
          file.print(pload.substring(8));
        }
        file.close();
        //loraSendMessage(String("coords"), pload.substring(8), String(++msgSent),true);
        sendPerimeterCoords();
        Serial.println(parseJSONPerCoords());
      }else if (pload.startsWith("setCollarName:")) {
        byte boardDest = byte(pload.substring(14,15)[0]);
        String boardName = pload.substring(15);
        loraSendMessage(String("conf"), "setBoardname:"+boardName, String(++msgSent),true);
      }
      break;
  }
}
void sendPerimeterCoords() {
  loraSendMessage(String("coords"), parseJSONPerCoords(), String(++msgSent), true);
}
String parseJSONPerCoords() {
  String output = "";

  File json = SPIFFS.open("/pCoords.json", FILE_READ);
  if (json) {
    while (json.available()) {
      char chRead = json.read();
      if (chRead != '{' && chRead != '}' && chRead != '[' && chRead != ']' && chRead != ':') {
        if (chRead == '"') {
          char innerChRead = json.read();
          String header = "";
          while (json.available() && innerChRead != '"') {
            header += innerChRead;
            innerChRead = json.read();
          }
        } else {
          output += chRead;
        }
      }
      //Serial.print(chRead);
    }
    //Serial.println();
  }
  json.close();

  return output;
}

void strSplit(char msg[]) {
  Serial.print(F("Parsing String: "));
  Serial.println(msg);
  char* ptr = strtok(msg, ",");
  byte i = 0;
  Serial.println(F("index\ttext\tNumeric Value"));
  while (ptr) {
    Serial.print(i);
    Serial.print(F("\t\""));
    Serial.print(ptr); // this is the ASCII text we want to transform into an integer
    Serial.print(F("\"\t"));
    Serial.println(atol(ptr)); // atol(ptr) will be your long int you could store in your array at position i. atol() info at http://www.cplusplus.com/reference/cstdlib/atol
    ptr = strtok(NULL, ",");
    i++;
  }
}
void loraSendMessage(String type, String val, String msgId, bool waitAck) {
  LoRa.beginPacket();
  LoRa.write(destination);
  LoRa.write(localAddress);
  //LoRa.write(outgoing.length());
  LoRa.print(type + ";");
  LoRa.print(val + ";");
  LoRa.print(msgId + ";");
  LoRa.endPacket();
  if (waitAck) {
    //expectedAck = String(localAddress ) + String(destination) + String("ack") + msgId + localAddress;
    Serial.println("Waiting for ack: " + msgId);
    expectedAck = msgId;
    int millisAckWait = 5000;
    bool sendAckToWebsocket = false;
    String ackWebsocket = type;
    if(type=="coords"){
      expectedAck = val;
      millisAckWait = 10000;
      Serial.println("PerCoords: " + val);
      sendAckToWebsocket = true;
    }
    long waitStart = millis();
    if (maxWaitStartTime == 0)
      maxWaitStartTime = waitStart;
    bool ackReceived = false;
    while (millis() < waitStart + 1500 && !ackReceived) {
      if(LoRa.parsePacket()!=0){
        int recipient = LoRa.read();
        int sender = LoRa.read();
        String type = loraParse(';');
        String val = loraParse(';');
        String msgId = loraParse(';');
        String incoming = loraParse(';');
        printPacketParts(recipient, sender, type, val, msgId, incoming);
        lastCollareRssi = LoRa.packetRssi();
        defaultMinsInterval = map(LoRa.packetRssi(),-15, -125, 5, 15);
        Serial.println("DefaultMinsInterval: " + String(defaultMinsInterval));
        writeOled(5, "RSSI: " + String(LoRa.packetRssi()) + " - " + String(map(LoRa.packetRssi(), -125, -15, 0, 100)) + "%");

        if(recipient == localAddress){
          if(type=="ack" && val==expectedAck){            
            Serial.println("Ack received: " + expectedAck);
            ackReceived = true;
            maxWaitStartTime = 0;
            webSocket.broadcastTXT(String("{\"type\":\"ack\",\"val\":\"") + ackWebsocket + String("\"}"));
            break;
          }else if(type=="ack" && val!=expectedAck){
            Serial.println("Wrong ack received: " + val + " expected: " + expectedAck);
            loraSendMessage(type,val,String(msgSent),waitAck);
            ackReceived = true;
            break;
          }else if(type!="ack"){
            exLoraAction(recipient, sender, type, val, msgId, incoming);
            //loraSendMessage(type,val,String(msgSent),waitAck);
          }
        }
      }    
      /*String rawMsg = loraReadRaw();
      if (rawMsg != "" && rawMsg != expectedAck) {
        Serial.println("Wrong ack received: " + rawMsg + " - " + expectedAck);
        loraSendMessage(type, val, String(++msgSent), waitAck);
        ackReceived = true;
        break;
      } else if (rawMsg == expectedAck) {
        Serial.println("Ack received: " + expectedAck);
        ackReceived = true;
        maxWaitStartTime = 0;
        break;
      }*/
    }
    if (!ackReceived && maxWaitStartTime + millisAckWait > millis()) {
      Serial.println("No ack received: " + type + " - " + msgId);
      loraSendMessage(type, val, msgId, waitAck);
    } else if (!ackReceived && maxWaitStartTime < millis()) {
      maxWaitStartTime = 0;
      Serial.println("Ignoring current message: " + type + " - " + msgId);
    }
  }
}
void loraSendRaw(String msg) {
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();
}
String loraReadRaw() {
  String msg = "";
  if (LoRa.parsePacket() == 0) return "";
  while (LoRa.available()) {
    msg += (char)LoRa.read();
  }
  return msg;
}
void loraOnReceive(int packetSize) {
  if (packetSize == 0) return;

  int recipient = LoRa.read();
  int sender = LoRa.read();
  //byte incomingLength = LoRa.read();
  String type = loraParse(';');
  String val = loraParse(';');
  String msgId = loraParse(';');
  String incoming = loraParse(';');
  lastCollareRssi = LoRa.packetRssi();
  defaultMinsInterval = map(LoRa.packetRssi(),-15, -125, 5, 15);
  Serial.println("DefaultMinsInterval: " + String(defaultMinsInterval));
  writeOled(5, "RSSI: " + String(LoRa.packetRssi()) + " - " + String(map(LoRa.packetRssi(), -125, -15, 0, 100)) + "%");

  /*if(!isCollareKnown(sender)){
    collare *tmpCollare = new collare(sender);  
    collari.add(tmpCollare);
  }*/
  
  if (recipient == localAddress) {
    exLoraAction(recipient, sender, type, val, msgId, incoming);
  }
}

void printPacketParts(int recipient, int sender, String type, String val, String msgId, String incoming){
  /*Serial.println("\nType: " + String(type));
  Serial.println("Val: " + val);
  Serial.println("Extra: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println("msgId: " + msgId);
  Serial.println();*/
}
void exLoraAction(int recipient, int sender, String type, String val, String msgId, String incoming){
  printPacketParts(recipient, sender, type, val, msgId, incoming);

  if (type == "bat") {
    //clearOledLine(2);
    int batPerc = calcBatteryPerc(val.toFloat()*1000);
    if(batPerc>0){
      writeOled(3, "Batt. Collare: " + String(batPerc) + "%");
      if (val.toFloat() <= 3.3){
        writeOled(4, "Caricare collare");
        /*if(val.toFloat() < 3.3){
          buzInterval = 2000;
        }*/
      }
    }
  } else if (type == "gps") {
    lastCollareMillis = millis();
    lastCollareGpsVal = val;
    lastCollareSender = sender;
    websocketSendGps(lastCollareSender, lastCollareGpsVal);
    //loraSendRaw(String(sender) + String(recipient) + String("ack") + msgId + sender);
  }else if(type == "beep"){
    buzSound = (val == "perout");
    buzInterval = 300;
    if(val=="perout")
      writeOled(4, "  Fuori perimetro");
    else
      clearOledLine(4);
  }

  if(type!="ack")
    sendAck(recipient, sender, msgId);

  writeOled(5, "RSSI: " + String(LoRa.packetRssi()) + " - " + String(map(LoRa.packetRssi(), -125, -15, 0, 100)) + "%");
}
void sendAck(int sender, int recipient, String msgId){
  //loraSendRaw(String(sender) + String(recipient) + String("ack") + msgId + sender);
  Serial.println("Sending ack: " + msgId);
  loraSendMessage("ack", msgId, String(++msgSent), false);
}

void websocketSendGps(int sender, String val){
  /*lastCollareSender = sender;
  lastCollareGpsVal = val;*/
  webSocket.broadcastTXT(String("{\"sender\":\"") + lastCollareSender + String("\",\"type\":\"gps\",\"val\":\"") + lastCollareGpsVal + String("\"}"));
}

String loraParse(char sep) {
  String msg = "";
  bool parsing = true;
  while (LoRa.available() && parsing) {
    char readChar = (char)LoRa.read();
    if (readChar != sep)
      msg += readChar;
    else
      parsing = false;
  }
  return msg;
}

String listSPIFFS() {
  String output = "";
  //Serial.println("SPIFFS Info: " + String(SPIFFS.usedBytes()) + " / " + String(SPIFFS.totalBytes()) + "B\n");
  File root = SPIFFS.open("/");

  File file = root.openNextFile();

  while (file) {

    //Serial.print("FILE: ");
    //Serial.println(file.name());
    output.concat(String(file.name()) + "\n");

    file = root.openNextFile();
  }
  return output;
}
String SPIFFSinfo() {
  String output = "Total: " + String(SPIFFS.totalBytes()) + " B\t" + "Used: " + String(SPIFFS.usedBytes()) + " B";
  return output;
}
void handleNotFound(AsyncWebServerRequest *request) { // if the requested file or page doesn't exist, return a 404 not found error
  String reqUrl = request->url();
  if (!handleFileRead(reqUrl, request)) {        // check if the file exists in the flash memory (SPIFFS), if so, send it
    request->send(404, "404: File Not Found", "text/plain");
  }
}
bool handleFileRead(String path, AsyncWebServerRequest *request) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  if (SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path, contentType);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Max-Age", "10000");
    response->addHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "*");
    //request->send(SPIFFS, path, contentType);
    request->send(response);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void cliProcess() {
  if (Serial.available() > 0) {
    String cmd = Serial.readString();
    String output = "";
    cmd.trim();
    if (cmd != "") {
      if (cmd == "listSPIFFS")
        output = listSPIFFS();
      else if (cmd == "SPIFFSinfo")
        output = SPIFFSinfo();
      else if (cmd == "batteryPerc")
        output = "BatteryPercentage: " + getAxpInfo(cmd);
      else if (cmd == "chargeCurrent")
        output = "ChargeCurrent: " + getAxpInfo(cmd) + " mA";
      else if (cmd == "displayOn")
        displaySetContrast(128);
      else if (cmd == "displayOff")
        displaySetContrast(0);
      else if (cmd == "buzzerOn"){
        output = "Turning buzzer ON";
        buzSound = true;
      }else if(cmd == "buzzerOff"){
        output = "Turning buzzer OFF";
        buzSound = false;
      }else if(cmd=="loraSendRandomMessage")
        loraSendMessage(String("RandomMsg"), String(random(0,50)), String(-1), true);

    }

    if (output != "")
      Serial.println(output);
  }
}

String getAxpInfo(String type) {
  String output;
  if (type == "chargeCurrent")
    output = String(getCurrentFromDef(axp.getChargeControlCur()));
  else if (type == "batteryPerc")
    output = String(axp.getBattPercentage());

  return output;
}
void powerDevice(String dev, String power) {
  bool innerPower = power == "ON" ? AXP202_ON : AXP202_OFF;

  if (dev == "GPS")
    axp.setPowerOutPut(AXP192_LDO3, innerPower);
  else if (dev == "OLED"){
    //axp.setPowerOutPut(AXP192_DCDC1, innerPower);
    displaySetContrast(power == "ON" ? 128 : 0);
    lastOledOn = millis();
  }
  else if (dev == "LORA")
    axp.setPowerOutPut(AXP192_LDO2, innerPower);
  else if (dev == "DC1")
    axp.setPowerOutPut(AXP192_DCDC2, innerPower);
}
void writeOled(int line, String txt) {
  int txtSize = 1;
  clearOledLine(line);
  display.setTextSize(txtSize);
  display.setTextColor(WHITE);
  display.setCursor(0, line * 10 * txtSize);
  // Display static text
  display.println(txt);
  display.display();
}
void clearOledLine(int line) {
  int y, x;
  for (y = (line * 10); y <= ((line * 10) + 9); y++) {
    for (x = 0; x < 127; x++) {
      display.drawPixel(x, y, BLACK);
    }
  }
}
void displaySetContrast(uint8_t contrast)
{
  if (contrast == 0) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  } else if (contrast > 0 && contrast <= 255) {
    displayMinContr = (contrast == 1);
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
  }
}

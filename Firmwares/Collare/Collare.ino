#include <LinkedList.h>
#include <axp20x.h>
#include <HardwareSerial.h>
#include "externalProcedures.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include <SPIFFS.h>
#include <FS.h>

#define FORMAT_SPIFFS_IF_FAILED true

#define SDA 21
#define SCL 22
#define csPin 18
#define resetPin 23
#define irqPin 38
#define defaultMinsInterval 7

//SoftwareSerial gpsSerial(34,12);
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

const char* ssid = "DogTracker";
const char* password = "";

AXP20X_Class axp;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

//byte localAddress = 0xFF;
//byte destination = 0xBB;
byte localAddress = 0xBB;
byte destination = 0xFF;
long lastSendTime = 0;
int minsInterval = defaultMinsInterval;
int interval = minsInterval *1000*60;
long msgSent = 0;
bool waitingAck = false;
long ackMsgId = -1;
String expectedAck = "";
long maxWaitStartTime = 0;
int lastIntoPerimeter = -1;
String boardName = "";

class coord{
  public:
    double lng;
    double lat;
};

LinkedList<coord*> perCoordLst = LinkedList<coord*>();

void setup(){
  Serial.begin(115200);
  beginWire(10);
  beginAxp(10);
  beginSpiffs();
  
  axp.setChargingTargetVoltage(AXP202_TARGET_VOL_4_2V);
  axp.setChargeControlCur(AXP1XX_CHARGE_CUR_550MA);
  axp.setShutdownTime(AXP_POWER_OFF_TIME_4S);
  axp.setlongPressTime(AXP_LONGPRESS_TIME_1S);
  
  powerDevice("OLED","OFF");
  powerDevice("GPS","ON");
  powerDevice("LORA","ON");
  powerDevice("DC1","OFF");
  
  beginLora();
  LoRa.setTxPower(20);
  
  gpsSerial.begin(9600, SERIAL_8N1, 34, 12);
  
  coordsStrToList(getPerCoords());
  Serial.println(getPerCoords());
  Serial.println();
  
  cycleLoraSend(true);
}

void loop(){
  cliProcess();
  loraOnReceive(LoRa.parsePacket());

  smartDelay(1);

  if (axp.isChargeing()) {
    axpLed("ON");
  } else {
    axpLed("OFF");
  }
  
  cycleLoraSend(false);
}

void cycleLoraSend(bool forced){
  if (forced || millis() - lastSendTime > interval) {
    float vbat = axp.getBattVoltage()/1000;
    
    for(int i=100;i>0 && vbat<0;i--){
      vbat = axp.getBattVoltage()/1000;
      delay(250);
    }
    if(vbat>0){
      loraSendMessage(String("bat") , String(vbat) , String(++msgSent), true);
      //delay(500);
    }
    loraSendMessage(String("gps") , (String(gps.location.lat(),8) + "-" + String(gps.location.lng(),8)) , String(++msgSent), true);
    lastSendTime = millis();

    if(gps.location.lng()!=0 && gps.location.lat()!=0 && gps.satellites.value() > 4){
      Serial.println("Nel perimetro: " + String((bool)lastIntoPerimeter?"SI":"NO"));
      loraSendMessage("beep", (String("per") + String((bool)lastIntoPerimeter?"in":"out")), String(++msgSent), true);
    }
  }
}

boolean beginWire(int tests){
  cleari2c(SCL,SDA);
  boolean wireRunning = Wire.begin(21, 22);
  if(wireRunning){
    Serial.println("Wire Begin PASS");
  }else if(tests>0){
    if(tests == 10)
      Serial.print("Wire Begin FAIL");
    else
      Serial.print(".");
    delay(5000);
    cleari2c(SCL,SDA);
    wireRunning = beginWire(--tests);
  }else{
    Serial.println("\nREQUIRED MANUAL REBOOT");
    while(true){}
  }
  return wireRunning;
}
boolean beginAxp(int tests){
  boolean isRunning = !axp.begin(Wire, AXP192_SLAVE_ADDRESS);
  if(isRunning){
    Serial.println("AXP Begin PASS");
  }else if(tests>0){
    if(tests == 10)
      Serial.print("AXP Begin FAIL");
    else
      Serial.print(".");
    isRunning = beginAxp(--tests);
  }
  return isRunning;
}
boolean beginDisplay(){
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.invertDisplay(false);
  display.clearDisplay();
}
void beginLora(){
  LoRa.setPins(csPin, resetPin, irqPin);
  if (!LoRa.begin(866E6)) {
    Serial.println("LoRa init failed. Check your connections.");
    while (true);
  }
  Serial.println("LoRa init succeeded.");
  //LoRa.enableCrc();
  LoRa.setTxPower(20);
}
void beginSpiffs(){
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    Serial.println("SPIFFS Mount Failed");
    return;
  }
}

bool pointInPolygon(float x, float y) {
  int polyCorners = perCoordLst.size();
  int i, j=polyCorners-1 ;
  bool oddNodes=false;
  for (i=0; i<polyCorners; i++) {
      if ((perCoordLst.get(i)->lat< y && perCoordLst.get(j)->lat>=y
      || perCoordLst.get(j)->lat< y && perCoordLst.get(i)->lat>=y)
      && (perCoordLst.get(i)->lng<=x || perCoordLst.get(j)->lng<=x)) {
        oddNodes^=(perCoordLst.get(i)->lng+(y-perCoordLst.get(i)->lat)/(perCoordLst.get(j)->lat-perCoordLst.get(i)->lat)*(perCoordLst.get(j)->lng-perCoordLst.get(i)->lng)<x);
      }
      j=i;
  }
  return oddNodes;
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
  if(waitAck){
    //expectedAck = String(localAddress) + String(destination) + String("ack") + msgId + localAddress;
    expectedAck = msgId;
    long waitStart = millis();
    if(maxWaitStartTime == 0)
      maxWaitStartTime = waitStart;
    bool ackReceived = false;
    while(millis() < waitStart+1000 && !ackReceived){
      /*String rawMsg = loraReadRaw();
      if(rawMsg!="" && rawMsg!=expectedAck){
        Serial.println("Wrong ack received: " + rawMsg + " - " + expectedAck);
        loraSendMessage(type,val,String(++msgSent),waitAck);
        ackReceived = true;
        break;
      }else if(rawMsg==expectedAck){
        Serial.println("Ack received: " + expectedAck);
        ackReceived = true;
        maxWaitStartTime = 0;
        break;
      }*/
      if(LoRa.parsePacket()!=0){
        int recipient = LoRa.read();
        int sender = LoRa.read();
        String type = loraParse(';');
        String val = loraParse(';');
        String msgId = loraParse(';');
        String incoming = loraParse(';');

        if(recipient == localAddress){
          if(type=="ack" && val==expectedAck){            
            Serial.println("Ack received: " + expectedAck);
            ackReceived = true;
            maxWaitStartTime = 0;
            break;
          }else if(type=="ack" && val!=expectedAck){
            Serial.println("Wrong ack received: " + val + " expected: " + expectedAck);
            loraSendMessage(type,val,String(msgSent),waitAck);
            ackReceived = true;
            break;
          }else if(type!="ack"){
            //exLoraAction(recipient, sender, type, val, msgId, incoming);
            //loraSendMessage(type,val,String(msgSent),waitAck);
            break;
          }
        }
      }      
    }
    if(!ackReceived && maxWaitStartTime + 5000 > millis()){
      Serial.println("No ack received");
      loraSendMessage(type,val,msgId,waitAck);
    }
  }
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
  
  if (recipient == localAddress) {
    exLoraAction(recipient, sender, type, val, msgId, incoming);
  }

}
void printPacketParts(int recipient, int sender, String type, String val, String msgId, String incoming){
  Serial.println("\nType: " + String(type));
  Serial.println("Val: " + val);
  Serial.println("Extra: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println("msgId: " + msgId);
  Serial.println();
}
void exLoraAction(int recipient, int sender, String type, String val, String msgId, String incoming){
  bool skipAck = false;
  printPacketParts(recipient, sender, type, val, msgId, incoming);
  
  if(type == "coords"){
    //loraSendRaw(String(sender) + String(recipient) + String("ack") + msgId + sender);
    //sendAck(sender,recipient,msgId);
    Serial.println("Received coords: " + val);
    saveCoords(val);
    sendAck(sender,recipient,String(val));
    skipAck = true;
  }else if(type == "conf"){
    if(val == "sendPos"){
      cycleLoraSend(true);
    }else if(val.startsWith("setBoardname:")){
      String bNm = val.substring(13);//splitStr(val,':',1);     
      File file = SPIFFS.open("/bConf.json", FILE_WRITE);
      if (file) {
        file.print("bName:"+bNm+"\n");
      }
      file.close();
      Serial.println("Received boardName: " + bNm);
    }else if(val == "getBoardname"){String output = "";
      File file = SPIFFS.open("/bConf.txt", FILE_READ);
      if(!file) return "Error reading file";
      else{
        while (file.available()) {
          String tmpRead = file.readString();
          if(tmpRead == "\n")
            break;
          output += tmpRead;
        }
      }
      file.close();
      loraSendMessage(String("conf") , , String(++msgSent), true);
    }
  }

  axpLed("ON");
  delay(150);
  axpLed("OFF");
  
  if(!skipAck && type!="ack")
    sendAck(sender,recipient,msgId);
}
String splitStr(String baseStr, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = baseStr.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (baseStr.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? baseStr.substring(strIndex[0], strIndex[1]) : "";
}

char* strToChr(String s){
    char p[s.length()]; 
    int i; 
    for (i = 0; i < sizeof(p); i++) { 
        p[i] = s[i];
    }
    return p;
}
long compute_hash(String s) {
    const int p = 31;
    const int m = 1e9 + 9;
    long hash_value = 0;
    long p_pow = 1;
    for (char c : s) {
        hash_value = (hash_value + (c - 'a' + 1) * p_pow) % m;
        p_pow = (p_pow * p) % m;
    }
    return hash_value;
}
/*size_t strHash(String cp)
{
    size_t hash = 5381;
    //while (*cp)
    for(int i=0;i<cp.length();i++)
        hash = 33 * hash ^ (unsigned char) *cp[i]++;
    return hash;
}*/
/*uint32_t jenkins_one_at_a_time_hash(char *key, size_t len)
{
    uint32_t hash, i;
    for(hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}*/
void sendAck(int sender, int recipient, String msgId){
  //loraSendRaw(String(sender) + String(recipient) + String("ack") + msgId + sender);
  Serial.println("Sending ack: " + msgId);
  loraSendMessage("ack", msgId, String(++msgSent), false);
}
void saveCoords(String strCoords){
  bool savError = true;

  for(int i = 0; i<5 && savError; i++){
    Serial.println("Trying to write: " + strCoords);
    File file = SPIFFS.open("/pCoord.txt", FILE_WRITE);
    if (!file){
      Serial.println("Error opening file");
      return;
    }

    savError = !file.print(strCoords);
    if(savError)
      Serial.println("File has been written");
    else{
      Serial.println("Error writing file");
      SPIFFS.format();
    }
  
    file.close();
    Serial.println("Written: " + getPerCoords());
  }
  coordsStrToList(strCoords);
}
String getPerCoords(){
  String output = "";
  File file = SPIFFS.open("/pCoord.txt", FILE_READ);
  if(!file) return "Error reading file";
  else{
    while (file.available()) {
      output += file.readString();
    }
  }
  file.close();
  return output;
}
void coordsStrToList(String strCoords){
  int commaCount = 1;
  coord point;
  String tmpVal = "";
  double tmpLng, tmpLat;
  perCoordLst.clear();
  for(int i=0;i<strCoords.length();i++){
    tmpVal += strCoords[i];
    if(strCoords[i]==',' || (i+1)==strCoords.length()){
      if(commaCount%2==0)
        tmpLat = tmpVal.toDouble();
      else 
        tmpLng = tmpVal.toDouble();
      if(commaCount%2==0){
        coord *tmpCoord = new coord();
        tmpCoord->lat = tmpLat;
        tmpCoord->lng = tmpLng;
        perCoordLst.add(tmpCoord);
      }
      tmpVal = "";
      commaCount++;
    }
  }
  Serial.println("PerPoints: " + String(perCoordLst.size()));
  printList();
}
void printList(){
  for(int i=0;i<perCoordLst.size();i++){
    coord *tmpCoord = perCoordLst.get(i);
    Serial.println("Lat: " + String(tmpCoord->lat,5));
    Serial.println("Lng: " + String(tmpCoord->lng,5) + "\n");
  }
}
void loraSendRaw(String msg) {
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();
}
String loraParse(char sep){
  String msg = "";
  bool parsing = true;
  while (LoRa.available() && parsing) {
    char readChar = (char)LoRa.read();
    if(readChar != sep)
      msg += readChar;
    else
      parsing = false;
  }
  return msg;
}
String loraReadRaw(){
  String msg = "";
  if (LoRa.parsePacket() == 0) return "";
  while (LoRa.available()){
    msg += (char)LoRa.read();
  }
  return msg;
}

void cliProcess(){
  if(Serial.available()>0){
    String cmd = Serial.readString();
    String output = "";
    cmd.trim();
    if(cmd!=""){
      if(cmd=="batteryPerc")
        output = "BatteryPercentage: " + getAxpInfo(cmd);
      else if(cmd=="chargeCurrent")
        output = "ChargeCurrent: " + getAxpInfo(cmd) + " mA";
      else if(cmd=="loraSendRandomMessage")
        loraSendMessage(String("RandomMsg"), String(random(0,50)), String(-1), true);
      else if(cmd=="getPerCoords")
        Serial.println("perCoords: " + getPerCoords());
    }

    if(output!="")
      Serial.println(output);
  }
}

String getAxpInfo(String type){
  String output;
  if(type == "chargeCurrent")
    output = String(getCurrentFromDef(axp.getChargeControlCur()));
  else if(type == "batteryPerc")
    output = String(axp.getBattPercentage());

  return output;
}
void powerDevice(String dev, String power){
  bool innerPower = power=="ON"?AXP202_ON:AXP202_OFF;
  
  if(dev=="GPS")
      axp.setPowerOutPut(AXP192_LDO3, innerPower);
  /*else if(dev=="OLED")
      displaySetContrast(power=="ON"?128:0);*/
      //axp.setPowerOutPut(AXP192_DCDC1, innerPower);
  else if(dev=="LORA")
      axp.setPowerOutPut(AXP192_LDO2, innerPower);
  else if(dev=="DC1")
      axp.setPowerOutPut(AXP192_DCDC2, innerPower);
}
void axpLed(String power){
  if(power=="ON")
    axp.setChgLEDMode(AXP20X_LED_LOW_LEVEL);  
  else
    axp.setChgLEDMode(AXP20X_LED_OFF);  
}
static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (gpsSerial.available())
      gps.encode(gpsSerial.read());

    int curIntoPerimeter = (int)pointInPolygon(gps.location.lng(),gps.location.lat());
    //Serial.println(String(curIntoPerimeter) + " - " + String(lastIntoPerimeter));
    if(lastIntoPerimeter!=curIntoPerimeter && gps.location.lng()!=0 && gps.location.lat()!=0 && gps.satellites.value() > 4){
      lastIntoPerimeter = curIntoPerimeter;
      Serial.println("Nel perimetro: " + String((bool)curIntoPerimeter?"SI":"NO"));
      cycleLoraSend(true);
      minsInterval = (bool)curIntoPerimeter?defaultMinsInterval:0.05;
      //interval = ((bool)curIntoPerimeter?3:0.05) *1000*60;
      loraSendMessage("beep", (String("per") + String((bool)curIntoPerimeter?"in":"out")), String(++msgSent), true);
    }
  } while (millis() - start < ms);
}
void printGPSdata(){
  Serial.print("Latitude  : ");
  Serial.println(gps.location.lat(), 5);
  Serial.print("Longitude : ");
  Serial.println(gps.location.lng(), 4);
  Serial.print("Satellites: ");
  Serial.println(gps.satellites.value());
  Serial.print("Altitude  : ");
  Serial.print(gps.altitude.feet() / 3.2808);
  Serial.println("M");
  Serial.print("Time      : ");
  Serial.print(gps.time.hour());
  Serial.print(":");
  Serial.print(gps.time.minute());
  Serial.print(":");
  Serial.println(gps.time.second());
  Serial.print("Speed     : ");
  Serial.println(gps.speed.kmph()); 
  Serial.println("**********************");

  smartDelay(1000);

  if (millis() > 5000 && gps.charsProcessed() < 10)
    Serial.println(F("No GPS data received: check wiring or power on module"));  
}

/*void writeOled(int line, String txt){
  int txtSize = 2;
  display.setTextSize(txtSize);
  display.setTextColor(WHITE);
  display.setCursor(0, line*10*txtSize);
  // Display static text
  display.println(txt);
  display.display(); 
}
void clearOledLine(int line){
  int y,x;
  for (y=(line*10); y<=((line*10)+10); y++){
    for (x=0; x<127; x++){
      display.drawPixel(x, y, BLACK);
    }
  }
}
void displaySetContrast(uint8_t contrast)
{
  if(contrast == 0){
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }else if(contrast>0 && contrast<=255){
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
  }
}*/

#include "HX711.h"
#include "M5StickCPlus.h"
#include <QubitroMqttClient.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

HX711 scale;

const char* ssid = ""; 
const char* password = "";

char deviceID[] = "";
char deviceToken[] = "";

uint8_t dataPin = 33;
uint8_t clockPin = 32;

//4 gram tolerance these scales are wack
uint8_t tolerance = 4;

//weight is current weight of bowls & water, intake is daily intake total
float weight, intake = 0;

//each bowl weight
uint16_t bowlWeight = 95;

// send data to qubitro only once per day
bool sentToday = false;
// use for hour of day
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "us.pool.ntp.org");
uint8_t timeOffset = 6;


WiFiClient wifiClient;
QubitroMqttClient mqttClient(wifiClient);

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  M5.Lcd.println();
  M5.Lcd.print("Connecting to ");
  M5.Lcd.println(ssid);

  //WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.println("");
  M5.Lcd.println("WiFi connected");
}

void qubitro_connect() {
  char host[] = "broker.qubitro.com";
  int port = 1883;
  if (!mqttClient.connect(host, port)){
    M5.Lcd.print("Connection failed. Error code: ");
    M5.Lcd.println(mqttClient.connectError());
  }
  M5.Lcd.println("Connected to Qubitro.");
  mqttClient.subscribe(deviceID);
  delay(5000);
}

void qubitro_init() {
  mqttClient.setId(deviceID);
  mqttClient.setDeviceIdToken(deviceID, deviceToken);
  M5.Lcd.println("Connecting to Qubitro...");
  qubitro_connect();
}

void setup() {
  M5.begin();
  Serial.begin(115200);
  setup_wifi();
  qubitro_init();
  timeClient.update();
  scale.begin(dataPin, clockPin);
  scale.set_offset(4294784307);
  scale.set_scale(16.290691);
  scale.tare();
  weight = scale.get_units(100);
}

void loop() {

  mqttClient.poll();

  Serial.print("UNITS: ");
  Serial.print(weight);

  float wNew = scale.get_units(100);

  Serial.print("\t\tUPDATED: ");
  Serial.print(wNew);
  Serial.print("\t\tDELTA: ");
  Serial.println(wNew - weight);
  
  // lower weight detected, ensure it was intake and not bowl removal
  if(wNew + tolerance < weight && weight - wNew < bowlWeight) {
    float delta = weight - wNew;
    intake += delta;
  }
  // weight increased
  else if(wNew > weight + tolerance) {
    // delay 20 seconds and check again to see if we refilled or if Rigby is drinking
    delay(20000);
    wNew = scale.get_units(100);
    Serial.print("Increase detected. Waited 20s and got new weight: ");
    Serial.print(wNew);
    Serial.print(".\t\tDELTA: ");
    Serial.println(wNew - weight);
    // was drinking
    if(wNew + tolerance < weight && weight - wNew < bowlWeight){
      float delta = weight - wNew;
      Serial.println(delta);
      intake += delta;
    }
  }

  //update weight regardless
  weight = wNew;

  Serial.print("INTAKE: ");
  Serial.println(intake);
  M5.Lcd.print("INTAKE: ");
  M5.Lcd.println(intake);

  // send data at 8pm
  timeClient.update();
  int8_t currHr = timeClient.getHours();
  currHr = currHr - timeOffset;
  if (currHr < 0) {
    currHr = 24 + currHr;
  }
  // 8 pm and we haven't sent yet
  if(currHr == 20 && !sentToday) {
    // did this to ensure intake doesn't get reset before it gets sent to qubitro
    if(!mqttClient.connected())
      qubitro_connect();
    float tmpIntake = intake;
    static char payload[256];
    snprintf(payload, sizeof(payload)-1, "{\"intake\":%f}", tmpIntake);
    mqttClient.beginMessage(deviceID);
    mqttClient.print(payload); 
    mqttClient.endMessage();  
    Serial.print("It's 8 pm. Sending intake: ");
    Serial.print(intake);
    intake = 0;
    sentToday = true;
  }
  // hit midnight, we haven't sent today anymore
  else if(currHr == 0) {
    sentToday = false;
  }

  mqttClient.poll();
  delay(60000); // 60 second delay (1 min)
}


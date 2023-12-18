// ---------- Libs ------------------
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <DHT_U.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// ---------- Variable ------------------
#define DHT11_PIN D7
#define RELAY_PIN D8

#define S0 D4
#define S1 D3
#define S2 D6
#define S3 D5
#define SIG A0

unsigned long previousMillis = 0;
const long interval = 10000; //10s

// Get UTC +7
const long utcOffsetInSeconds = 7 * 3600;

char weekDays[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
String months[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);


int dhtSensor;
float temperature;
float humidity;
int waterSensor;
int soilMoistureSensor;
bool waterRequest = false;
bool isNotified = false;
String message = "";

// Notification to mobile: Using IFTTT
const char* host = "maker.ifttt.com";

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT11_PIN, DHT11);

// Wifi information
const char* ssid = "iPhone của Tùng Lâm";
const char* password = "khongcopass";

//***Set server***
const char* mqttServer = "broker.hivemq.com"; 
int port = 1883;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void wifiConnect() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
}

void mqttConnect() {
  while(!mqttClient.connected()) {
    Serial.println("Attemping MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if(mqttClient.connect(clientId.c_str())) {
      Serial.println("Server connected");
      //***Subscribe all topic you need***
      mqttClient.subscribe("iot12/requestWater");
    }
    else {
      Serial.println("Try again in 5 seconds...");
      delay(5000);
    }
  }
}

//MQTT Receiver
void callback(char* topic, byte* message, unsigned int length) {
  Serial.println(topic);
  String strMsg;
  for(int i=0; i<length; i++) {
    strMsg += (char)message[i];
  }
  Serial.println(strMsg);

  //***Code here to process the received package***
  if(strcmp(topic,"iot12/requestWater") == 0){
    waterRequest = true;
  }
}

void MQTTConfig()
{
  wifiConnect();
  mqttClient.setServer(mqttServer, port);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive( 90 );
}

void setup(void)
{ 
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(SIG, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  Serial.begin(9600);
  Serial.print("Connecting to WiFi");
  // DHT11 setup
  dht.begin();
  // LCD setup
  lcd.init();
  lcd.clear();
  lcd.backlight();
  // Initial Message
  lcd.print("Welcome back!");
  // MQTT server setup
  MQTTConfig();
  timeClient.begin();
}

void connectToServer()
{
  if(!mqttClient.connected()) {
    mqttConnect();
  }
  mqttClient.loop();
}

String getCurrentDate() {
  String formattedTime = timeClient.getFormattedTime();

  String weekDay = weekDays[timeClient.getDay()];

  //Get a time structure
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime); 

  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  String currentMonthName = months[currentMonth-1];
  int currentYear = ptm->tm_year+1900;
  //Print complete date:
  String currentDate = weekDay + ", " + String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay) + " : " + formattedTime;
  Serial.println("Current Date: " + currentDate);
  return currentDate;
}

void loop() {
  connectToServer();
  // Thong so nhiet do, do am DHT11
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  Serial.println("Nhiệt độ: " + String(temperature));
  Serial.println("Độ ẩm: " + String(humidity));
  
  // Thong so thoi gian thuc te
  timeClient.update();
  String currentDate = getCurrentDate();

  // Thong so do am dat
  digitalWrite(S0, LOW); digitalWrite(S1, LOW); digitalWrite(S2, LOW); digitalWrite(S3, LOW);
  soilMoistureSensor = analogRead(SIG);
  Serial.println("Độ ẩm đất: " + String(soilMoistureSensor));
  // Thong so muc nuoc
  digitalWrite(S0, HIGH); digitalWrite(S1, LOW); digitalWrite(S2, LOW); digitalWrite(S3, LOW);
  waterSensor = analogRead(SIG);
  Serial.println("Mực nước: " + String(waterSensor));

  int soilMoisturePercentage = map(soilMoistureSensor, 0, 1023, 100, 0);
  int waterLevelPercentage = map(waterSensor, 0, 1023, 0, 100);

  // Che do tu dong tuoi cay dua vao nhiet do, do am khong khi, do am dat
  Serial.println("Water Condition: " + String(isWater(soilMoistureSensor, waterLevelPercentage, temperature) || waterRequest));
  if(isWater(soilMoistureSensor, waterLevelPercentage, temperature) || waterRequest) {
    char history[100];
    currentDate.toCharArray(history, sizeof(history));
    mqttClient.publish("iot12/waterHistory", history);
    Serial.println("Bắt đầu tưới nước...");
    lcd.print("Auto watering...");
    water();
    Serial.println("Đã tưới nước xong...");
    lcd.clear();
    waterRequest = false;
  }
  
  Serial.println("Notified: " + String(isNotified));
  
  // Kiem tra luong nuoc va push notification
  if(waterLevelPercentage < 12 && !isNotified){
    char warning[50];
    message = "Your jar is empty, please refill water!";
    message.toCharArray(warning, sizeof(warning));
    mqttClient.publish("iot12/waterWarning", warning);
    isNotified = true;
    Serial.println("Đã gửi thông báo hết nước!");
  }
  if(waterLevelPercentage >= 12)
    isNotified = false;

  // Hien thi thong tin len LCD
  lcd.clear();

  lcd.print("Soil moisture:");
  lcd.setCursor(6, 1);
  lcd.print(String(soilMoisturePercentage) + "%");
  delay(2000);
  lcd.clear();

  lcd.print("Water Level:");
  lcd.setCursor(6, 1);
  lcd.print(String(waterLevelPercentage) + "%");
  delay(2000);
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Temp: " + String(temperature) + String((char)223) + "C");
  lcd.setCursor(0, 1);
  lcd.print("Humidity: " + String(humidity) + "%");
  delay(2000);
  lcd.clear();
  //----------------------------------------------------------------
  //***Publish data to MQTT Server***
  // data sent to MQTT Server must be a string
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // Save the last time you published
    previousMillis = currentMillis;
    float sensorValues[] = {soilMoisturePercentage, waterLevelPercentage, temperature, humidity};
    char buffer[50];
    String valueString = "";
    for (int i = 0; i < sizeof(sensorValues) / sizeof(sensorValues[0]); i++) {
      valueString += String(sensorValues[i], 2);
      if (i < sizeof(sensorValues) / sizeof(sensorValues[0]) - 1) {
        valueString += ",";
      }
    }
    // valueString += currentDate;
    valueString.toCharArray(buffer, sizeof(buffer));
    Serial.println("Publish sensor values to mqtt server: " + valueString);
    mqttClient.publish("iot12/list_sensor_values", buffer);
  }
}

int isWater(int moistureValue, int waterValue, float tempValue)
{
  // 0: Khong tuoi
  // 1: Tuoi
  // Soil is too dry
  if(moistureValue <= 750) return 0;
  // Too cold or too hot
  if(tempValue < 15 || tempValue > 35) return 0;
  // Het nuoc
  if(waterValue < 12) return 0;
  return 1;
}

void water() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(10000);
  digitalWrite(RELAY_PIN, LOW);
}
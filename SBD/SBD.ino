#include "DHT.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <UrlEncode.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define DPIN 4
#define DTYPE DHT11
DHT dht(DPIN, DTYPE);

#define MQ135_PIN 14
#define PIR_PIN 5
#define BUZZER_PIN 13
#define ALARM_LED_PIN 18
#define LIGHT_LED_PIN 19
#define BUTTON_SENSOR_PIN 25
#define BUTTON_ALARM_PIN 26
#define RESET_BUTTON_PIN 27 


bool sensorEnabled = false;
bool alarmPIREnabled = false;
bool alarmMQEnabled = false;
int sensorStateMQ = digitalRead(MQ135_PIN);
unsigned long activationStartTime = 0;
bool pirReady = false;
bool pirMessageSent = false; 
bool gasMessageSent = false;   
bool tempHumMessageSent = false;         

Preferences preferences;

String ssid = "";
String password = "";
String phoneNumber = "";
String apiKey = "";

WebServer server(80);  
DNSServer dnsServer;   

void setup() {
  dht.begin();

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MQ135_PIN, INPUT);

  pinMode(ALARM_LED_PIN, OUTPUT);
  pinMode(LIGHT_LED_PIN, OUTPUT);

  pinMode(BUTTON_SENSOR_PIN, INPUT_PULLUP);
  pinMode(BUTTON_ALARM_PIN, INPUT_PULLUP);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(ALARM_LED_PIN, LOW);
  digitalWrite(LIGHT_LED_PIN, HIGH);

  display.begin(0x3C);
  display.cp437(true);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);

  displayMessage("Power ON");
  delay(2000);

  preferences.begin("config", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  phoneNumber = preferences.getString("phone", "");
  apiKey = preferences.getString("apikey", "");
  preferences.end();

  const char* apSSID = "ESP32_Config";
  WiFi.softAP(apSSID);

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();

  if (!ssid.isEmpty() && !password.isEmpty()) {
    WiFi.begin(ssid.c_str(), password.c_str());
    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 10000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
      display.clearDisplay();

      display.setTextSize(2);
      int connectingWidth = 10 * 12; 
      int connectingX = (128 - connectingWidth) / 2; 
      display.setCursor(connectingX, 10); 
      display.println("Connecting");

      int toWiFiWidth = 7 * 12; 
      int toWiFiX = (128 - toWiFiWidth) / 2; 
      display.setCursor(toWiFiX, 36); 
      display.println("to WiFi");

      display.display();
    }

    if (WiFi.status() == WL_CONNECTED) {
      displayMessage("Connected");
      sendMessage("Hello from ESP32!");
    } else {
      displayMessage("No WiFi");
    }
  } else {
    displayMessage("Setup AP");
  }
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    resetConfig(); 
    delay(300);    
  }

  if (digitalRead(BUTTON_SENSOR_PIN) == LOW) {
    sensorEnabled = !sensorEnabled;

    if (sensorEnabled) {
      displayMessage("Secured");
      activationStartTime = millis();
      pirReady = false;
      displayData();
    } else {
      displayMessage("Unsecured");
    }
    delay(300);
  }

  if (digitalRead(BUTTON_ALARM_PIN) == LOW) {

    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(ALARM_LED_PIN, LOW);
    digitalWrite(LIGHT_LED_PIN, HIGH);

    if (alarmPIREnabled) {
      alarmPIREnabled = false;
      pirMessageSent = false;
      sensorEnabled = false;  
      pirReady = false;

      displayMessage("Alarm OFF");
      delay(2000);
      displayMessage("Unsecured");
    }

    else if (alarmMQEnabled) {
      alarmMQEnabled = false;
      gasMessageSent = false;

      displayMessage("Alarm OFF");
    }

    delay(300);
  }

  if (sensorEnabled && !pirReady) {
    if (millis() - activationStartTime >= 60000) {
      pirReady = true;
    }
  }

  if (sensorEnabled && pirReady) {
    int pirState = digitalRead(PIR_PIN);
    if (pirState == HIGH) {
      alarmPIREnabled = true;
      if (!pirMessageSent && WiFi.status() == WL_CONNECTED) {
        sendMessage("ALERT! Motion detected!");
        pirMessageSent = true;
      }
    }
  }

  sensorStateMQ = digitalRead(MQ135_PIN);
  if (sensorStateMQ == LOW) {
    if (!alarmMQEnabled) {
      alarmMQEnabled = true;
      if (!gasMessageSent && WiFi.status() == WL_CONNECTED) {
        sendMessage("ALERT! Gas leak detected!");
        gasMessageSent = true;
      }
    }
  }

  if (alarmPIREnabled || alarmMQEnabled) {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(ALARM_LED_PIN, HIGH);
    digitalWrite(LIGHT_LED_PIN, LOW);

    display.clearDisplay();
    display.setTextSize(2);

    if (alarmPIREnabled) {
      display.setCursor(28, 16);
      display.println("Motion");
      display.setCursor(16, 32);
      display.println("detected");
    }
    if (alarmMQEnabled) {
      display.setCursor(46, 16);
      display.println("Gas");
      display.setCursor(16, 32);
      display.println("detected");
    }

    display.display();
    delay(100);
    return;
  }

    if ((temperature > 30.0 || humidity > 70.0) && !tempHumMessageSent) {
    if (WiFi.status() == WL_CONNECTED) {
      sendMessage("ALERT! High temperature or humidity detected!");
      tempHumMessageSent = true;
    }
  }

  if (!sensorEnabled) {
    displayData();
  }
}

void displayData() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Temp:");
  display.setTextSize(2);
  display.setCursor(0, 13);
  display.print(temperature);
  display.print((char)167);
  display.println("C");

  display.setTextSize(1);
  display.setCursor(0, 37);
  display.println("Humidity:");
  display.setTextSize(2);
  display.setCursor(0, 49);
  display.print(humidity);
  display.println("%");

  display.display();
}

void displayMessage(const char *message) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor((SCREEN_WIDTH - strlen(message) * 12) / 2, 28);
  display.println(message);
  display.display();
  delay(2000);
}

void sendMessage(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber + "&apikey=" + apiKey + "&text=" + urlEncode(message);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpResponseCode = http.POST(url);
    http.end();
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>ESP32 Config</title></head><body>";
  html += "<h1>ESP32 Configuration</h1>";
  html += "<form action='/save' method='POST'>";
  html += "WiFi SSID: <input type='text' name='ssid'><br>";
  html += "WiFi Password: <input type='password' name='password'><br>";
  html += "Phone Number: <input type='text' name='phone'><br>";
  html += "API Key: <input type='text' name='apikey'><br>";
  html += "<input type='submit' value='Save'>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("phone") && server.hasArg("apikey")) {
    ssid = server.arg("ssid");
    password = server.arg("password");
    phoneNumber = server.arg("phone");
    apiKey = server.arg("apikey");

    preferences.begin("config", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("phone", phoneNumber);
    preferences.putString("apikey", apiKey);
    preferences.end();

    server.send(200, "text/html", "<h1>Configuration Saved! Restart ESP32.</h1>");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<h1>Missing fields!</h1>");
  }
}

void resetConfig() {
  preferences.begin("config", false);
  preferences.clear();  
  preferences.end();

  display.clearDisplay();

  display.setTextSize(2); 

  int configWidth = 6 * 12; 
  int configX = (128 - configWidth) / 2; 
  display.setCursor(configX, 10); 
  display.println("Config"); 

  int resetWidth = 5 * 12; 
  int resetX = (128 - resetWidth) / 2; 
  display.setCursor(resetX, 36); 
  display.println("Reset");

  display.display(); 
  delay(2000);
  ESP.restart();  
}

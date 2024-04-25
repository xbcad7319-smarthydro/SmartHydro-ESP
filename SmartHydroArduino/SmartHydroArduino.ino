#include <SPI.h>
#include <WiFi.h>
#include <DHT.h>
#include "DFRobot_PH.h"
#include "DFRobot_EC10.h"
#include <arduino-timer.h>

// WiFi network settings
const char *ssid = "SmartHydro";              // newtork SSID (name). 8 or more characters
const char *password = "Hydro123!";           // network password. 8 or more characters
const IPAddress ESP_IP(192, 168, 8, 14);    // create an IP address
const IPAddress SUBNET(255, 255, 255, 0);     // create an IP address
const IPAddress LEASE_START(192, 168, 8, 20);  // create an IP address

WiFiServer server(80);
String header = "";
String message = "";

// AI
#include "EC.h"
#include "pH.h"
#include "Humidity.h"
#include "Temperature.h"
Eloquent::ML::Port::RandomForestEC ForestEC;
Eloquent::ML::Port::RandomForestpH ForestPH;
Eloquent::ML::Port::RandomForestHumidity ForestHumidity;
Eloquent::ML::Port::RandomForestTemperature ForestTemperature;

#define FLOW_PIN 34
#define LIGHT_PIN 35
#define EC_PIN 36
#define PH_PIN 39
#define DHT_PIN 25
#define DHTTYPE DHT22

#define LED_PIN 26
#define FAN_PIN 27
#define PUMP_PIN 14
#define EXTRACTOR_PIN 32

#define PH_UP_PIN 23
#define PH_DOWN_PIN 22
#define EC_UP_PIN 21
#define EC_DOWN_PIN 19


DFRobot_PH ph;
DHT dht = DHT(DHT_PIN, DHTTYPE);

const unsigned long SIXTEEN_HR = 57600000;
const unsigned long PUMP_INTERVAL = 5000;
const unsigned long EIGHT_HR = 28800000;
const unsigned long FOUR_HR = 14400000;

DFRobot_EC10 ec;
auto timer = timer_create_default();
float temperature;
float humidity;
float ecLevel;
float phLevel;
float lightLevel;
float flowRate;
volatile int pulseCount = 0;
unsigned long currentTime, cloopTime;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(ssid, password);
  //WiFi.softAPConfig(ESP_IP, SUBNET, ESP_IP, LEASE_START);
  //WiFi.setHostname("SmartHydro-Tent");

  Serial.println("Access point started");

  // Start the server
  server.begin();
  ec.begin();
  dht.begin();
  ph.begin();
  Serial.println("Sensors started");

  pinMode(26, OUTPUT);
  pinMode(27, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(32, OUTPUT);

  pinMode(23, OUTPUT);
  pinMode(22, OUTPUT);
  pinMode(21, OUTPUT);
  pinMode(19, OUTPUT);

  pinMode(34, INPUT);
  pinMode(35, INPUT);
  pinMode(36, INPUT);
  pinMode(39, INPUT);
  pinMode(25, INPUT);
  attachInterrupt(0, incrementPulseCounter, RISING);
  sei();

  Serial.println("Pins configured");

  // turning on equipment that should be on by default
  togglePin(LED_PIN);
  togglePin(FAN_PIN);
  togglePin(PUMP_PIN);
  togglePin(EXTRACTOR_PIN);

  timer.every(5000, (bool (*)(void *))estimateTemperature);
  timer.every(5000, (bool (*)(void *))estimateHumidity);
  timer.every(SIXTEEN_HR, (bool (*)(void *))estimateEC);
  timer.every(SIXTEEN_HR, (bool (*)(void *))estimatePH);
  Serial.println("Timers set");
  Serial.println(WiFi.softAPIP());
  toggleLightOn();
}


void loop() {
  WiFiClient client = server.available();  // Check if a client has connected
  header = "";
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  lightLevel = getLightLevel();
  ecLevel = getEC();
  phLevel = getPH();
  flowRate = getFlowRate();

  timer.tick();

  if (client) {  // If a client is available
    message = "";
    while (client.connected()) {  // Loop while the client is connected
      if (client.available()) {   // Check if data is available from the client
        char c = client.read();
        //Serial.write(c); // Echo received data to Serial Monitor
        header += c;
        // you got two newline characters in a row
        // that's the end of the HTTP request, so send a response
        if (header.indexOf("/sensors") > 0) {
          message = "{\n  \"PH\": \"" + String(phLevel) + "\",\n  \"Light\": \"" + String(lightLevel) + "\",\n  \"EC\": \"" + String(ecLevel) + "\",\n  \"FlowRate\": \"" + String(flowRate) + "\",\n  \"Humidity\": \"" + String(humidity) + "\",\n  \"Temperature\": \"" + String(temperature) + "\"\n }";
          ec.calibration(ecLevel, temperature);

          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/light") > 0) {
          togglePin(LED_PIN);
        }

        if (header.indexOf("/fan") > 0) {
          togglePin(FAN_PIN);
        }

        if (header.indexOf("/extract") > 0) {
          togglePin(EXTRACTOR_PIN);
        }

        if (header.indexOf("/pump") > 0) {
          togglePin(PUMP_PIN);
        }

        if (header.indexOf("/phUp") > 0) {
          togglePin(PH_DOWN_PIN, LOW);
          togglePin(PH_UP_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disablePH);
        }

        if (header.indexOf("/phDown") > 0) {
          togglePin(PH_UP_PIN, LOW);
          togglePin(PH_DOWN_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disablePH);
        }

        if (header.indexOf("/ecUp") > 0) {
          togglePin(EC_DOWN_PIN, LOW);
          togglePin(EC_UP_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disableEC);
        }

        if (header.indexOf("/ecDown") > 0) {
          togglePin(EC_UP_PIN, LOW);
          togglePin(EC_DOWN_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disableEC);
        }

        if (header.indexOf("/ph") > 0) {
          disablePH();
        }

        if (header.indexOf("/ec") > 0) {
          disableEC();
        }

        if (header.indexOf("/components") > 0) {
          message = "{\n  \"PHPump\": \"" + String(digitalRead(PH_UP_PIN)) + "\",\n  \"Light\": \"" + String(digitalRead(LIGHT_PIN)) + "\",\n  \"ECPump\": \"" + String(digitalRead(EC_UP_PIN)) + "\",\n  \"WaterPump\": \"" + String(digitalRead(PUMP_PIN)) + "\",\n  \"Exctractor\": \"" + String(digitalRead(EXTRACTOR_PIN)) + "\",\n  \"Fan\": \"" + String(digitalRead(FAN_PIN)) + "\"\n }";

          sendHttpResponse(client, message);
        }
      }
    }
    Serial.println("Client disconnected");
    client.stop();  // Close the connection with the client
  }
}


/**
  * Inverts the reading of a pin.
  */
void togglePin(int pin) {
  digitalWrite(pin, !(digitalRead(pin)));
}

void togglePin(int pin, int toggleValue) {
  digitalWrite(pin, toggleValue);
}

/**
  * Sends a http response along with a message.
  */
void sendHttpResponse(WiFiClient client, String message) {
  client.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n");

  if (message.length() > 0) {
    client.print("Content-Length:" + String(message.length()) + "\r\n\r\n");
    Serial.print(message.length());
    client.print(message);
  }
}

float getLightLevel() {
  return analogRead(LIGHT_PIN);
}

float getEC() {
  float ecVoltage = (float)analogRead(EC_PIN) / 1024.0 * 5000.0;
  return ec.readEC(ecVoltage, temperature);
}

float getPH() {
  float phVoltage = analogRead(PH_PIN) / 1024.0 * 5000;
  return ph.readPH(phVoltage, temperature);
}

void setComponent(int result, int pin, int status) {
  if (result == 0) {  // Below Optimal
    if (status == 1) {
      digitalWrite(pin, LOW);
      //Serial.println("FAN offfffff");
    }
  } else if (result == 1) {  // Above Optimal
    if (status == 0) {
      digitalWrite(pin, HIGH);
      //Serial.println("FAN ON");
    }
  } else {
    if (status == 0) {  //Optimal
      Serial.println("Component: " + digitalRead(pin));
      togglePin(pin);
      //Serial.println("COMPONENT OFF!!!!!!!");
    }
  }
}

void setPump(int result, int pinUp, int pinDown, int statusUp, int statusDown) {
  if (result == 0) {  //Below Optimal
    if (statusUp == 1 || statusDown == 0) {
      digitalWrite(pinUp, LOW);
      digitalWrite(pinDown, HIGH);
      Serial.println("pump up offfffff");
    }
  } else if (result == 1) {  //Above Optimal
    if (statusUp == 0 || statusDown == 1) {
      digitalWrite(pinUp, HIGH);
      digitalWrite(pinDown, LOW);
      Serial.println("pump down on");
    }
  } else {

    Serial.println("Component: " + digitalRead(pinUp));
    Serial.println("Component: " + digitalRead(pinDown));

    togglePin(pinUp, HIGH);
    togglePin(pinDown, HIGH);
    Serial.println("COMPONENTS OFF!");
  }
}

void estimateTemperature() {
  if (temperature != NAN) {
    int result = ForestTemperature.predict(&temperature);
    int fanStatus = digitalRead(FAN_PIN);
    int lightStatus = digitalRead(LIGHT_PIN);
    Serial.println(result);

    setComponent(result, FAN_PIN, fanStatus);
  }
}

void estimateHumidity() {
  if (humidity != NAN) {
    int result = ForestHumidity.predict(&humidity);
    int extractorStatus = digitalRead(EXTRACTOR_PIN);

    setComponent(result, EXTRACTOR_PIN, extractorStatus);
  }
}

void estimatePH() {
  if (phLevel != NAN) {
    int result = ForestPH.predict(&phLevel);
    int phUpStatus = digitalRead(PH_UP_PIN);
    int phDownStatus = digitalRead(PH_DOWN_PIN);

    setPump(result, PH_UP_PIN, PH_DOWN_PIN, phUpStatus, phDownStatus);
    timer.in(PUMP_INTERVAL, (bool (*)(void *))disablePH);
  }
}

void estimateEC() {
  if (ecLevel != NAN) {
    int result = ForestEC.predict(&ecLevel);
    int ecUpStatus = digitalRead(EC_UP_PIN);
    int ecDownStatus = digitalRead(EC_DOWN_PIN);

    setPump(result, EC_UP_PIN, EC_DOWN_PIN, ecUpStatus, ecDownStatus);
    timer.in(PUMP_INTERVAL, (bool (*)(void *))disableEC);
  }
}

void estimateFactors() {
  estimatePH();
  estimateTemperature();
  estimateHumidity();
  estimateEC();
}

void disablePH() {
  digitalWrite(PH_UP_PIN, HIGH);
  digitalWrite(PH_DOWN_PIN, HIGH);
}

void disableEC() {
  digitalWrite(EC_UP_PIN, HIGH);
  digitalWrite(EC_DOWN_PIN, HIGH);
  Serial.println("EC HIT");
}

void incrementPulseCounter() {
  pulseCount++;
}

float getFlowRate() {
  currentTime = millis();

  if (currentTime >= (cloopTime + 1000)) {
    cloopTime = currentTime;
    float flowRatePerHr = (pulseCount * 60 / 7.5);
    pulseCount = 0;
    return flowRatePerHr;
  }
}

void toggleLightOn() {
  togglePin(LED_PIN, LOW);
  timer.in(EIGHT_HR, (bool (*)(void *))toggleLightOff);
}

void toggleLightOff() {
  togglePin(LED_PIN, HIGH);
  timer.in(FOUR_HR, (bool (*)(void *))toggleLightOn);
}

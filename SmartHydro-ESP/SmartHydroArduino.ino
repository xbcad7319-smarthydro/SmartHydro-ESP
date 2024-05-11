#include <SPI.h>
#include <WiFi.h>
#include <DHT.h>
#include "DFRobot_PH.h"
#include "DFRobot_EC10.h"
#include <arduino-timer.h>
#include "DFRobot_ESP_PH.h"
#include "DFRobot_ESP_EC.h"
#include "EEPROM.h"

// WiFi network settings
const char *ssid = "SmartHydro_DEV";           // newtork SSID (name). 8 or more characters
const char *password = "Hydro123!";            // network password. 8 or more characters
const IPAddress ESP_IP(192, 168, 8, 14);       // create an IP address
const IPAddress SUBNET(255, 255, 255, 0);      // create an IP address
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

#define FLOW_PIN 36
#define LIGHT_PIN 33
#define EC_PIN 34
#define PH_PIN 35
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

DFRobot_ESP_EC ec;
DFRobot_ESP_PH ph;
DHT dht = DHT(DHT_PIN, DHTTYPE);

const unsigned long SIXTEEN_HR = 57600000;
const unsigned long PUMP_INTERVAL = 5000;
const unsigned long EIGHT_HR = 28800000;
const unsigned long FOUR_HR = 14400000;

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

  pinMode(LED_PIN, OUTPUT);
  pinMode(EXTRACTOR_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(EXTRACTOR_PIN, HIGH);
  digitalWrite(FAN_PIN, HIGH);
  digitalWrite(PUMP_PIN, HIGH);


  pinMode(PH_UP_PIN, OUTPUT);
  pinMode(PH_DOWN_PIN, OUTPUT);
  pinMode(EC_UP_PIN, OUTPUT);
  pinMode(EC_DOWN_PIN, OUTPUT);
  digitalWrite(PH_UP_PIN, HIGH);
  digitalWrite(PH_DOWN_PIN, HIGH);
  digitalWrite(EC_UP_PIN, HIGH);
  digitalWrite(EC_DOWN_PIN, HIGH);

  pinMode(FLOW_PIN, INPUT);
  pinMode(PH_PIN, INPUT);
  pinMode(EC_PIN, INPUT);
  pinMode(DHT_PIN, INPUT);
  pinMode(LIGHT_PIN, INPUT);
  attachInterrupt(0, incrementPulseCounter, RISING);
  sei();
  EEPROM.begin(32);
  ec.begin();
  dht.begin();
  ph.begin();


  Serial.println("Pins configured");

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
  ec.calibration(ecLevel, temperature);
  ph.calibration(phLevel, temperature);
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
          message = "{\n  \"PH\": \"" + String(phLevel) + "\",\n  \"Light\": \"" + String(lightLevel) + "\",\n  \"EC\": \"" + String(ecLevel) + "\",\n  \"FlowRate\": \"" + -1 + "\",\n  \"Humidity\": \"" + String(humidity) + "\",\n  \"Temperature\": \"" + String(temperature) + "\"\n }";
          
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/light") > 0) {
          togglePin(LED_PIN);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/fan") > 0) {
          togglePin(FAN_PIN);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/extract") > 0) {
          togglePin(EXTRACTOR_PIN);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/pump") > 0) {
          togglePin(PUMP_PIN);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/phUp") > 0) {
          togglePin(PH_DOWN_PIN, LOW);
          togglePin(PH_UP_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disablePH);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/phDown") > 0) {
          togglePin(PH_UP_PIN, LOW);
          togglePin(PH_DOWN_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disablePH);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/ecUp") > 0) {
          togglePin(EC_DOWN_PIN, LOW);
          togglePin(EC_UP_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disableEC);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/ecDown") > 0) {
          togglePin(EC_UP_PIN, LOW);
          togglePin(EC_DOWN_PIN, HIGH);
          timer.in(PUMP_INTERVAL, (bool (*)(void *))disableEC);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/ph") > 0) {
          disablePH();
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/ec") > 0) {
          disableEC();
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/components") > 0) {
          message = "{\n  \"PHPump\": \"" + String(digitalRead(PH_UP_PIN)) + "\",\n  \"Light\": \"" + String(digitalRead(LIGHT_PIN)) + "\",\n  \"ECPump\": \"" + String(digitalRead(EC_UP_PIN)) + "\",\n  \"WaterPump\": \"" + String(digitalRead(PUMP_PIN)) + "\",\n  \"Exctractor\": \"" + String(digitalRead(EXTRACTOR_PIN)) + "\",\n  \"Fan\": \"" + String(digitalRead(FAN_PIN)) + "\"\n }";

          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/allOff") > 0) {
          digitalWrite(PH_UP_PIN, HIGH);
          digitalWrite(PH_DOWN_PIN, HIGH);
          digitalWrite(EC_UP_PIN, HIGH);
          digitalWrite(EC_DOWN_PIN, HIGH);
          digitalWrite(LED_PIN, HIGH);
          digitalWrite(EXTRACTOR_PIN, HIGH);
          digitalWrite(FAN_PIN, HIGH);
          digitalWrite(PUMP_PIN, HIGH);
          sendHttpResponse(client, message);
          break;
        }

        if (header.indexOf("/allOn") > 0) {
          digitalWrite(PH_UP_PIN, LOW);
          digitalWrite(PH_DOWN_PIN, LOW);
          digitalWrite(EC_UP_PIN, LOW);
          digitalWrite(EC_DOWN_PIN, LOW);
          digitalWrite(LED_PIN, LOW);
          digitalWrite(EXTRACTOR_PIN, LOW);
          digitalWrite(FAN_PIN, LOW);
          digitalWrite(PUMP_PIN, LOW);
          sendHttpResponse(client, message);
          break;
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
  float ecVoltage = (float)analogRead(EC_PIN) / 4096.0 * 3300.0;
  return ec.readEC(ecVoltage, temperature);
}

float getPH() {
  float phVoltage = (float)analogRead(PH_PIN) / 4096.0 * 3300.0;
  return ph.readPH(phVoltage, temperature);
}

void setComponent(int result, int pin, int status) {
  if (result == 0) {  // Below Optimal
    if (status == 1) {
      digitalWrite(pin, HIGH);
      //Serial.println("FAN offfffff");
    }
  } else if (result == 1) {  // Above Optimal
    if (status == 0) {
      digitalWrite(pin, LOW);
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
      digitalWrite(pinUp, HIGH);
      digitalWrite(pinDown, LOW);
      Serial.println("pump up offfffff");
    }
  } else if (result == 1) {  //Above Optimal
    if (statusUp == 0 || statusDown == 1) {
      digitalWrite(pinUp, LOW);
      digitalWrite(pinDown, HIGH);
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

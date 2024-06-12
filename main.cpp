#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HttpClient.h>
#include <WiFi.h>
#include <inttypes.h>
#include <stdio.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "SparkFunLSM6DSO.h"
#include "Wire.h"

#define WHITE_PIN 25
#define BLUE_PIN 33
#define GREEN_PIN 32
#define BUZZER_PIN 15
#define INACTIVITY_MILLIS 10000
#define DISPLAY_MILLIS 1000
#define CHART_MILLIS 5000

TFT_eSPI tft = TFT_eSPI();
LSM6DSO myIMU;

const float threshold = 0.3;
const int stepDelay = 600;
float offsetX = 0.0;
float offsetY = 0.0;
unsigned long lastStepTime, currentTime;
int numSteps = 0;
int prevSteps = 0;

String startStr = "Steps Taken: ";
String startProgress = "Progress: ";
String stepsTaken;
String progress;
String congrats;
String result;

unsigned long i_timer, b_timer, d_timer, chart_timer;
bool inactive = false;
bool buzzer_on = false;
bool all_milestones_reached = false;
bool milestone_reached = false;
bool milestone1_reached = false;
bool milestone2_reached = false;
bool milestone3_reached = false;
bool milestone1_msg_sent = false;
bool milestone2_msg_sent = false;
bool milestone3_msg_sent = false;

int milestone1 = 10;
int milestone2 = 15;
int milestone3 = 20;

char ssid[50]; // your network SSID (name)
char pass[50]; // your network password (use for WPA, or use
               // as key for WEP)

// Name of the server we want to connect to
const char kHostname[] = "worldtimeapi.org";
// Path to download (this is the bit after the hostname in the URL
// that you want to download
const char kPath[] = "/api/timezone/Europe/London.txt";

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

String urlEncode(const char* s) {
  const char *hex = "0123456789ABCDEF";
  String encoded = "";
  while (*s) {
    if (('a' <= *s && *s <= 'z') || ('A' <= *s && *s <= 'Z') || ('0' <= *s && *s <= '9')) {
      encoded += *s;
    } else {
      encoded += '%';
      encoded += hex[*s >> 4];
      encoded += hex[*s & 15];
    }
    s++;
  }
  return encoded;
}

void nvs_access() {
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Open
  Serial.printf("\n");
  Serial.printf("Opening Non-Volatile Storage (NVS) handle... ");
  nvs_handle_t my_handle;
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  } else {
    Serial.printf("Done\n");
    Serial.printf("Retrieving SSID/PASSWD\n");
    size_t ssid_len;
    size_t pass_len;
    err = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
    err |= nvs_get_str(my_handle, "pass", pass, &pass_len);
    switch (err) {
      case ESP_OK:
        Serial.printf("Done\n");
        //Serial.printf("SSID = %s\n", ssid);
        //Serial.printf("PASSWD = %s\n", pass);
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        Serial.printf("The value is not initialized yet!\n");
        break;
      default:
        Serial.printf("Error (%s) reading!\n", esp_err_to_name(err));
    }
  }

  // Close
  nvs_close(my_handle);
}

void aws_write(String result, int steps, int currentSteps) {
  int err = 0;
  WiFiClient c;
  HttpClient http(c);

  String url = String("/?var=") + urlEncode(result.c_str()) + "&steps=" + String(steps) + "&current=" + String(currentSteps);
  err = http.get("13.52.101.251", 5000, url.c_str(), NULL);
  if (err == 0) {
    Serial.println("startedRequest ok");

    err = http.responseStatusCode();
    if (err >= 0) {
      Serial.print("Got status code: ");
      Serial.println(err);

      // Usually you'd check that the response code is 200 or a
      // similar "success" code (200-299) before carrying on,
      // but we'll print out whatever response we get

      err = http.skipResponseHeaders();
      if (err >= 0) {
        int bodyLen = http.contentLength();
        Serial.print("Content length is: ");
        Serial.println(bodyLen);
        Serial.println();
        Serial.println("Body returned follows:");

        // Now we've got to the body, so we can print it out
        unsigned long timeoutStart = millis();
        char c;
        // Whilst we haven't timed out & haven't reached the end of the body
        while ((http.connected() || http.available()) && ((millis() - timeoutStart) < kNetworkTimeout)) {
          if (http.available()) {
            c = http.read();
            // Print out this character
            Serial.print(c);

            bodyLen--;
            // We read something, reset the timeout counter
            timeoutStart = millis();
          } else {
            // We haven't got any data, so let's pause to allow some to
            // arrive
            delay(kNetworkDelay);
          }
        }
      } else {
        Serial.print("Failed to skip response headers: ");
        Serial.println(err);
      }
    } else {
        Serial.print("Getting response failed: ");
        Serial.println(err);
    }
  } else {
    Serial.print("Connect failed: ");
    Serial.println(err);
  }
  http.stop();
}

void calibrateIMU() {
  float sumX = 0.0;
  float sumY = 0.0;

  for (int i = 0; i < 100; i++) {
    sumX += myIMU.readFloatAccelX();
    delay(20);
  }
  offsetX = sumX / 100;
  delay(100);

  for (int i = 0; i < 100; i++) {
    sumY += myIMU.readFloatAccelY();
    delay(20);
  }
  offsetY = sumY / 100;
  delay(100);
}

void setup() {
  Serial.begin(9600);
  Wire.begin();

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  pinMode(WHITE_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  if (myIMU.begin()) {
    Serial.println("IMU ready");
  } else { 
    Serial.println("Could not connect to IMU");
    Serial.println("Freezing");
    while(1);
  }

  if (myIMU.initialize(BASIC_SETTINGS)) {
    Serial.println("Loaded settings");
  }

  calibrateIMU();

  stepsTaken = startStr + 0;
  progress = startProgress + "0%";
  Serial.println(stepsTaken);
  Serial.println(progress);

  delay(1000);
  // Retrieve SSID/PASSWD from flash before anything else
  nvs_access();

  // We start by connecting to a WiFi network
  delay(1000);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("MAC address: ");
  Serial.println(WiFi.macAddress());

  i_timer = d_timer = chart_timer = millis();
}

void loop() {
  currentTime = millis();

  if (currentTime - d_timer > DISPLAY_MILLIS) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.fillScreen(TFT_BLACK);

    if (milestone_reached) {
      milestone_reached = false;
      tft.setCursor((tft.width() - tft.textWidth("Good job!")) / 2, ((tft.height() - tft.fontHeight()) / 2) - 20);
      tft.println("Good job!");
      tft.setCursor((tft.width() - tft.textWidth("You reached")) / 2, ((tft.height() - tft.fontHeight()) / 2));
      tft.println("You reached");
      tft.setCursor((tft.width() - tft.textWidth(congrats)) / 2, ((tft.height() - tft.fontHeight()) / 2) + 20);
      tft.println(congrats);

      if (!milestone1_msg_sent && milestone1_reached) {
        milestone1_msg_sent = true;
        digitalWrite(WHITE_PIN, HIGH);
        delay(2000);
        digitalWrite(WHITE_PIN, LOW);
      }
      else if (!milestone2_msg_sent && milestone2_reached) {
        milestone2_msg_sent = true;
        digitalWrite(BLUE_PIN, HIGH);
        delay(2000);
        digitalWrite(BLUE_PIN, LOW);
      }
      else if (!milestone3_msg_sent && milestone3_reached) {
        milestone3_msg_sent = true;
        digitalWrite(GREEN_PIN, HIGH);
        delay(2000);
        digitalWrite(GREEN_PIN, LOW);
      }
    }
    else if (!inactive) {
      tft.setCursor((tft.width() - tft.textWidth(stepsTaken)) / 2, ((tft.height() - tft.fontHeight()) / 2) - 10);
      tft.println(stepsTaken);
      tft.setCursor((tft.width() - tft.textWidth(progress)) / 2, ((tft.height() - tft.fontHeight()) / 2) + 10);
      tft.println(progress);
    }
    else {
      tft.setCursor((tft.width() - tft.textWidth("Walking time!")) / 2, (tft.height() - tft.fontHeight()) / 2);
      tft.println("Walking time!");
    }

    d_timer = currentTime;
  }

  float currentX = myIMU.readFloatAccelX() - offsetX;
  delay(1);
  float currentY = myIMU.readFloatAccelY() - offsetY;
  delay(1);

  float totalA = sqrt(sq(currentX) + sq(currentY));

  if (totalA > threshold && (currentTime - lastStepTime) > stepDelay) {
    noTone(BUZZER_PIN);
    i_timer = currentTime;
    inactive = false;

    numSteps++;
    lastStepTime = currentTime;
    stepsTaken = startStr + numSteps;
    progress = startProgress + min(100, int((float(numSteps) / float(milestone3)) * 100)) + "%";

    Serial.println(stepsTaken);
    Serial.println(progress);

    result = stepsTaken + "\n" + progress + "\nMilestone 1 (" + milestone1 + " steps): ";
    if (!milestone1_reached) {
      result += "Not ";
    }
    result += "Reached\nMilestone 2 (";
    result += milestone2;
    result += " steps): ";
    if (!milestone2_reached) {
      result += "Not ";
    }
    result += "Reached\nMilestone 3 (";
    result += milestone3;
    result += " steps): ";
    if (!milestone3_reached) {
      result += "Not ";
    }
    result += "Reached";
  }

  if (!all_milestones_reached && !inactive && millis() - i_timer > INACTIVITY_MILLIS) {
    inactive = true;
    b_timer = 1751;
  }

  if (inactive) {
    if (millis() - b_timer > 1750 && !buzzer_on) {
      tone(BUZZER_PIN, 100);
      buzzer_on = true;
      b_timer = millis();
    }
    else if (millis() - b_timer > 250 && buzzer_on) {
      noTone(BUZZER_PIN);
      buzzer_on = false;
      b_timer = millis();
    }
  }

  if (!milestone3_reached && numSteps >= milestone3) {
    congrats = String(milestone3) + " steps!";
    milestone_reached = true;
    milestone3_reached = true;
    all_milestones_reached = true;
  }
  else if (!milestone2_reached && numSteps >= milestone2) {
    congrats = String(milestone2) + " steps!";
    milestone_reached = true;
    milestone2_reached = true;
  }
  else if (!milestone1_reached && numSteps >= milestone1) {
    congrats = String(milestone1) + " steps!";
    milestone_reached = true;
    milestone1_reached = true;
  }

  if (currentTime - chart_timer > CHART_MILLIS) {
    int currentSteps = numSteps - prevSteps;
    prevSteps = numSteps;

    Serial.print("Steps within 10 sec interval: ");
    Serial.println(currentSteps);

    aws_write(result, numSteps, currentSteps);

    chart_timer = millis();
  }
  delay(20);
}
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#define CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO false
#define CONFIG_CAMERA_TASK_STACK 6144
#define CONFIG_CAMERA_PSRAM_DMA 1
#include "esp_camera.h"
using namespace websockets;

const char* ssid     = "POCO";
const char* password = "wizkid6884";


// GPIO 14 = TX, GPIO 15 = RX 
#define RXD1 15  // to Nano TX
#define TXD1 14  // to Nano RX


WebsocketsClient faceClient;
const char* face_server = "ws://10.99.66.184:8080";

#define FLASH_GPIO_NUM 4
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22



void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_XGA;
    config.jpeg_quality = 4;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera init failed");
  } else {
    Serial.println("✅ Camera init OK");
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QVGA);
  }
}

String lastResponse = "";

void onMessageCallback(WebsocketsMessage msg) {
  lastResponse = msg.data();
}

String sendPhoto(camera_fb_t *fb, const String &purpose, unsigned long timeout = 5000) {
    Serial.println("sending phote!");
    if (!faceClient.available()) {
        return "ERROR: Not connected";
    }
    lastResponse = "";
    String header = "{\"type\":\"photo\",\"purpose\":\"" + purpose + "\",\"length\":" + String(fb->len) + "}";
    faceClient.send(header);
    Serial.println("sent header!");
    faceClient.sendBinary((const char*)fb->buf, fb->len);
    Serial.println("sent phote!");
    unsigned long start = millis();

    while (lastResponse == "") {
        faceClient.poll();
        if (millis() - start > timeout) {
            return "ERROR: Timeout";
        }
    }
    Serial.println("got anwer!");
    if (lastResponse.indexOf("granted") >= 0) return "granted";
    else if (lastResponse.indexOf("denied") >= 0) return "denied";
    else return "error";

    
}


void setup() {
  // Debug monitor on USB
  Serial.begin(115200);

  // UART for Arduino Nano
  Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1);

  Serial.println("ESP32-CAM booting...");
  pinMode(FLASH_GPIO_NUM, OUTPUT);

  // === Connect to WiFi ===
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("✅ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (faceClient.connect(face_server)) {
    Serial.println("✅ WS connected");
  } else {
    Serial.println("❌ WS connect failed");
  }

  // Init camera
  initCamera();

  faceClient.onMessage(onMessageCallback);

  delay(5000);
}

void loop() {

  
  // Only listen if WiFi is still connected
  if (WiFi.status() == WL_CONNECTED) {
    faceClient.poll();
    if (Serial1.available()) {
      String cmd = Serial1.readStringUntil('\n');
      cmd.trim();
      Serial.print("Received from Nano: ");
      Serial.println(cmd);

      if (cmd.equalsIgnoreCase("check")) {
        digitalWrite(FLASH_GPIO_NUM, HIGH);
        delay(500);
        camera_fb_t *fb = esp_camera_fb_get();
        digitalWrite(FLASH_GPIO_NUM, LOW);
        if (fb) {
          String resp = sendPhoto(fb, "check");
          esp_camera_fb_return(fb);
          Serial1.println(String(resp));
          Serial.println(String("Server reply : ") + resp);
        }
        else {Serial1.println("Couldn't inintilize camera");
        Serial.println("Couldn't inintilize camera");}
      }
      else if (cmd.equalsIgnoreCase("register")) {
        digitalWrite(FLASH_GPIO_NUM, HIGH);
        delay(500);
        camera_fb_t *fb = esp_camera_fb_get();
        digitalWrite(FLASH_GPIO_NUM , LOW);
        if (fb) {
          String resp = sendPhoto(fb, "add");
          esp_camera_fb_return(fb);
          Serial.println(resp);
          Serial1.println(resp);
        }
        else {Serial1.println("Couldn't inintilize camera");
        Serial.println("Couldn't inintilize camera");}
      }
      else {
        //Serial1.println("unknown_wifi");
        //Serial.println("Reply: unknown_wifi");
      }
    }
  } else {
    // If WiFi is disconnected, try to reconnect
    Serial.println("⚠️ WiFi lost! Reconnecting...");
    WiFi.reconnect();
    delay(2000);
  }
}

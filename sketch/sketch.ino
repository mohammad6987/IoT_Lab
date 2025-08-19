#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <SPI.h>
#include <MFRC522.h>
#define CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO false
#define CONFIG_CAMERA_TASK_STACK 6144
#define CONFIG_CAMERA_PSRAM_DMA 1
#include "esp_camera.h"
using namespace websockets;  

// ================= WiFi =================
const char *ssid = "POCO";
const char *password = "wizkid6884";

// ================= RFID =================
#define SS_PIN 13
#define RST_PIN 2
MFRC522 mfrc522(SS_PIN, RST_PIN);

unsigned long lastCardRead = 0;
const unsigned long CARD_READ_COOLDOWN = 3000;
bool waitingForNewCard = false;
bool cardAuthorized = false;

// ================= WebSockets =================
WebsocketsClient faceClient;
WebsocketsClient cardClient;

const char* face_server = "ws://10.252.208.122:80"; 
const char* card_server = "ws://10.252.208.122:82"; 

// ================= Camera =================
// OV2640 settings
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

// ================= Helper Functions =================
String uidToString(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uid += "0";
    uid += String(buffer[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void printMemoryInfo() {
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
  if (psramFound()) {
    Serial.printf("PSRAM Size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  }
}

void checkMemory() {
  if (ESP.getFreeHeap() < 10000) {
    Serial.println("Warning: Low memory!");
  }
}

// ========== Camera Init ==========
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
    Serial.println("found PSRAM!!!!!!!!!");
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 8;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  printMemoryInfo();
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera init failed");
  } else {
    Serial.println("✅ Camera init OK");
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QVGA);
    Serial.println("set FrameSize!");
  }
}

// ========== Capture & Send Photo ==========
String sendPhoto(camera_fb_t *fb, const String &purpose, unsigned long timeout = 5000) {
    if (!faceClient.available()) {
        Serial.println("⛔ Face Server is not connected!");
        return "ERROR: Not connected";
    }

    // Step 1: Send purpose header
    String header = "{\"type\":\"photo\",\"purpose\":\"" + purpose + "\",\"length\":" + String(fb->len) + "}";
    faceClient.send(header);
    Serial.println("📩 Sent photo header: " + header);

    // Step 2: Send image
    faceClient.sendBinary((const char*)fb->buf, fb->len);
    Serial.println("📸 Photo sent to face server.");

    // Step 3: Wait for response
    unsigned long start = millis();
    String response = "";

    while (response == "") {
        faceClient.poll();
        if (millis() - start > timeout) {
            return "ERROR: Timeout";
        }
    }

    return response;
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  checkMemory();
  printMemoryInfo();
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected.");

  // Init RFID
  SPI.begin(14, 12, 15);
  mfrc522.PCD_Init();

  // Connect WebSocket Face Server
  faceClient.onMessage([](WebsocketsMessage msg){
    Serial.println("📩 Face Server: " + msg.data());
  });
  faceClient.onEvent([](WebsocketsEvent event, String data){
    if(event == WebsocketsEvent::ConnectionOpened){
        Serial.println("✅ Face WS connected");
    } else if(event == WebsocketsEvent::ConnectionClosed){
        Serial.println("❌ Face WS disconnected");
    }
  });

  if (faceClient.connect(face_server)) {
    Serial.println("✅ WebSocket connected!");
  } else {
    Serial.println("❌ WebSocket connect failed!");
  }

  // Init camera
  initCamera();
}

// ================= Loop =================
void loop() {
  faceClient.poll();  // keep WS alive

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
  } else {
    sendPhoto(fb, "add");
    delay(5000);
    sendPhoto(fb, "check");
    esp_camera_fb_return(fb);
  }

  delay(10000);
}

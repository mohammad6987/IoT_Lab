#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <SPI.h>
#include <MFRC522.h>
#define CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX 32768
#define CONFIG_CAMERA_JPEG_MODE_FRAME_SIZE_AUTO false
#define CONFIG_CAMERA_TASK_STACK 6244
#define CONFIG_CAMERA_PSRAM_DMA 1
#include "esp_camera.h"
using namespace websockets;


const char *ssid = "POCO";
const char *password = "wizkid6884";


#define SS_PIN 13
#define RST_PIN 2
MFRC522 mfrc522(SS_PIN, RST_PIN);

String MASTER_UID = "63A34416";

unsigned long lastCardRead = 0;
const unsigned long CARD_READ_COOLDOWN = 3000;
bool waitingForNewCard = false;
bool cardAuthorized = false;
String lastScannedUID = "";


WebsocketsClient faceClient;
WebsocketsClient cardClient;

const char* face_server = "ws://10.252.208.122:80";
const char* card_server = "ws://10.252.208.122:84";


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
  config.xclk_freq_hz = 8000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    Serial.println("found PSRAM!!!!!!!!!");
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 20;
    config.fb_count = 2;
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


String sendPhoto(camera_fb_t *fb, const String &purpose, unsigned long timeout = 5000) {
  if (!faceClient.available()) {
    Serial.println("⛔ Face Server is not connected!");
    return "ERROR: Not connected";
  }

 
  String header = "{\"type\":\"photo\",\"purpose\":\"" + purpose + "\",\"length\":" + String(fb->len) + "}";
  faceClient.send(header);
  Serial.println("📩 Sent photo header: " + header);


  faceClient.sendBinary((const char*)fb->buf, fb->len);
  Serial.println("📸 Photo sent to face server.");


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


void onCardMessage(WebsocketsMessage message) {
  Serial.println("📩 Card Server: " + message.data());
  

  String data = message.data();
  if (data.indexOf("\"status\":\"authorized\"") >= 0) {
    Serial.println("✅ Card authorized. Taking photo...");
    cardAuthorized = true;
  } else if (data.indexOf("\"status\":\"unauthorized\"") >= 0) {
    Serial.println("❌ Card not authorized.");
    cardAuthorized = false;
  } else if (data.indexOf("\"status\":\"added\"") >= 0) {
    Serial.println("✅ Card added to database.");
    waitingForNewCard = false;
  }
}


void connectCardServer() {
  if (cardClient.connect(card_server)) {
    Serial.println("✅ Connected to card server!");
    cardClient.onMessage(onCardMessage);
  } else {
    Serial.println("❌ Failed to connect to card server!");
  }
}


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


  SPI.begin(14, 12, 15,2);
  mfrc522.PCD_Init();


  faceClient.onMessage([](WebsocketsMessage msg){
    Serial.println("📩 Face Server: " + msg.data());
  });

  if (faceClient.connect(face_server)) {
    Serial.println("✅ Connected to face server!");
  } else {
    Serial.println("❌ Failed to connect to face server!");
  }


  connectCardServer();


  initCamera();
  
  Serial.println("System ready. Scan an RFID card...");
}


void loop() {
  faceClient.poll();
  cardClient.poll();


  if (!cardClient.available()) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 5000) {
      connectCardServer();
      lastReconnectAttempt = millis();
    }
  }


  if (millis() - lastCardRead < CARD_READ_COOLDOWN) return;
  
  
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  lastCardRead = millis();
  String uidStr = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println("Card UID: " + uidStr);
  lastScannedUID = uidStr;


  if (uidStr == MASTER_UID) {
    Serial.println("🔑 Master card detected. Ready to add new card.");
    waitingForNewCard = true;
  } 
  // Check if we're in "add new card" mode
  else if (waitingForNewCard) {
    if (cardClient.available()) {
      String addMsg = "{\"action\":\"add\",\"uid\":\"" + uidStr + "\"}";
      cardClient.send(addMsg);
      Serial.println("📤 Sent add request: " + addMsg);
    } else {
      Serial.println("❌ Card server not available for adding card");
    }
  } 
  // Normal card check
  else {
    if (cardClient.available()) {
      String checkMsg = "{\"action\":\"check\",\"uid\":\"" + uidStr + "\"}";
      cardClient.send(checkMsg);
      Serial.println("📤 Sent check request: " + checkMsg);
    } else {
      Serial.println("❌ Card server not available for checking card");
    }
  }

  mfrc522.PICC_HaltA();

  if (cardAuthorized) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      sendPhoto(fb, "recognition");
      esp_camera_fb_return(fb);
    } else {
      Serial.println("❌ Failed to capture image");
    }
    cardAuthorized = false;
  }
  
  delay(500); 
}
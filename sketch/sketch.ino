
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#define CONFIG_CAMERA_TASK_STACK_SIZE 10240
#include "esp_camera.h"
#include <WiFi.h>
#include <ArduinoWebsockets.h>
using namespace websockets;

// --- Wi-Fi ---
const char* ssid     = "POCO";
const char* password = "wizkid6884";




// --- WebSocket server ---
const char* ws_server_url = "ws://192.168.117.130:81"; // Change to your server

WebsocketsClient wsClient;

// --- RFID ---
#define RST_PIN    13
#define SS_PIN     2
#define FLASH_PIN  4

#define EEPROM_SIZE 4096
#define CARD_SIZE   12
#define MAX_CARDS   (EEPROM_SIZE / CARD_SIZE)

byte masterUID[4] = {0x63, 0xA3, 0x44, 0x16};
MFRC522 mfrc522(SS_PIN, RST_PIN);
bool waitingForNewCard = false;

// --- Camera pins (AI-Thinker) ---
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = 32;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 18;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera init failed!");
    return false;
  }
  return true;
}
void takePhotoAndSend() {
  camera_fb_t *fb = nullptr;

  // Try to capture a frame
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed!");
    return; // don't continue if capture failed
  }

  Serial.printf("📷 Captured frame: %d bytes\n", fb->len);

  // Try sending the photo
  bool success = false;
  if (wsClient.available()) {
    // Wrap in try/catch-like protection
    success = wsClient.sendBinary((const char*)fb->buf, fb->len);
    if (!success) {
      Serial.println("⚠️ WebSocket send failed.");
    } else {
      Serial.println("✅ Image sent over WebSocket.");
    }
  } else {
    Serial.println("⚠️ WebSocket not connected, skipping send.");
  }

  // Always release frame buffer to avoid memory leaks
  esp_camera_fb_return(fb);
  fb = nullptr;
}


void saveCardToEEPROM(byte *uid, const char *pass) {
  for (int slot = 0; slot < MAX_CARDS; slot++) {
    int addr = slot * CARD_SIZE;
    if (EEPROM.read(addr) == 0xFF) {
      for (int i = 0; i < 4; i++) EEPROM.write(addr + i, uid[i]);
      for (int i = 0; i < 8; i++) EEPROM.write(addr + 4 + i, pass[i]);
      EEPROM.commit();
      Serial.print("✅ Card saved in slot ");
      Serial.println(slot);
      return;
    }
  }
  Serial.println("⚠️ EEPROM full!");
}

bool findCardInEEPROM(byte *uid, char *passOut) {
  for (int slot = 0; slot < MAX_CARDS; slot++) {
    int addr = slot * CARD_SIZE;
    if (EEPROM.read(addr) == 0xFF) continue;

    bool match = true;
    for (int i = 0; i < 4; i++) {
      if (EEPROM.read(addr + i) != uid[i]) { match = false; break; }
    }
    if (match) {
      for (int i = 0; i < 8; i++) passOut[i] = EEPROM.read(addr + 4 + i);
      passOut[8] = '\0';
      return true;
    }
  }
  return false;
}

void printUID(byte *uid, byte size) {
  for (byte i = 0; i < size; i++) {
    Serial.print(uid[i] < 0x10 ? " 0" : " ");
    Serial.print(uid[i], HEX);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  // Init camera
  if (!initCamera()) {
    Serial.println("Camera init failed, stopping...");
    while (true);
  }

  // Init RFID
  SPI.begin(14, 12, 15);
  mfrc522.PCD_Init();

  // Wi-Fi connect
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("✅ Connected!");

  // Connect WebSocket
  if (wsClient.connect(ws_server_url)) {
    Serial.println("✅ WebSocket connected!");
  } else {
    Serial.println("❌ WebSocket connect failed!");
  }

  Serial.println("Place your card near the reader...");
}

void loop() {
  wsClient.poll(); // keep WS alive

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  Serial.print("Card UID:");
  printUID(mfrc522.uid.uidByte, mfrc522.uid.size);

  if (waitingForNewCard) {
    saveCardToEEPROM(mfrc522.uid.uidByte, "00000000");
    Serial.println("📷 Taking photo after registration...");
    takePhotoAndSend();
    waitingForNewCard = false;
  }
  else if (memcmp(mfrc522.uid.uidByte, masterUID, 4) == 0) {
    Serial.println("🔑 Master card detected. Present next card to add.");
    waitingForNewCard = true;
  }
  else {
    char pass[9];
    if (findCardInEEPROM(mfrc522.uid.uidByte, pass)) {
      Serial.print("✅ Trusted card detected. Password: ");
      Serial.println(pass);
      Serial.println("📷 Taking photo after recognition...");
      takePhotoAndSend();
    } else {
      Serial.println("❌ Unknown card.");
    }
  }

  mfrc522.PICC_HaltA();
}
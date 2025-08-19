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

const char* face_server = "ws://10.252.208.122:81";

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
    config.jpeg_quality = 12;
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
    Serial.println("Got Sensor");
    s->set_framesize(s, FRAMESIZE_QVGA);
    Serial.println("set FrameSize!");
}
  
}

// ========== Capture & Send Photo ==========
void takePhotoAndSend() {
  if (!faceClient.available()) return;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  Serial.printf("📸 Captured photo, size: %d bytes\n", fb->len);

  // Send binary data over WebSocket
  faceClient.sendBinary(fb->buf, fb->len);
  Serial.println("📤 Photo sent to face server.");

  esp_camera_fb_return(fb);
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
  checkMemory();
  printMemoryInfo();

  // Init RFID
  SPI.begin(14, 12, 15);
  mfrc522.PCD_Init();

  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  uint8_t response = SPI.transfer(0x00);
  SPI.endTransaction();

  Serial.print("SPI test response: 0x");
  Serial.println(response, HEX);
  checkMemory();
  printMemoryInfo();

  if (faceClient.connect(face_server)) {
    Serial.println("✅ WebSocket connected!");
  } else {
    Serial.println("❌ WebSocket connect failed!");
  }

  checkMemory();
  printMemoryInfo();


  delay(1000);
  // Init camera
  initCamera();
  checkMemory();
  

  // Connect WS4
} 



String cardServerResponse = "";

bool waitingCardResponse = false;


void setupWebSocket() {
    faceClient.onMessage([](WebsocketsMessage message) {
        Serial.print("Face server message: ");
        Serial.println(message.data());
        // handle response
    });

    faceClient.onEvent([](WebsocketsEvent event, String data) {
        if(event == WebsocketsEvent::ConnectionOpened){
            Serial.println("Face WS connected");
        } else if(event == WebsocketsEvent::ConnectionClosed){
            Serial.println("Face WS disconnected");
        } else if(event == WebsocketsEvent::GotPing){
            Serial.println("Ping received");
        } else if(event == WebsocketsEvent::GotPong){
            Serial.println("Pong received");
        }
    });
}



String sendCardRequestAndWait(String msg, unsigned long timeout = 5000) {
  if (!cardClient.avaiable()) return "ERROR: Not connected";

  waitingCardResponse = true;
  cardClient.sendTXT(msg);

  unsigned long start = millis();
  while (waitingCardResponse) {
    cardClient.loop(); // keep WebSocket alive
    if (millis() - start > timeout) {
      waitingCardResponse = false;
      return "ERROR: Timeout";
    }
  }
  return cardServerResponse;
}


String photoServerResponse = "";
bool waitingPhotoResponse = false;



String sendPhoto(camera_fb_t *fb, const String &purpose, unsigned long timeout = 5000) {
    if (!faceClient.available()) {
        Serial.println("⛔ Face Server is not connected!");
        return "ERROR: Not connected";
    }

    // Step 1: Send purpose header
    String header = "{\"type\":\"photo\",\"purpose\":\"" + purpose + "\",\"length\":" + String(fb->len) + "}";
    faceClient.sendTXT(header);
    Serial.println("📩 Sent photo header: " + header);

    // Step 2: Send image
    faceClient.sendBIN(fb->buf, fb->len);
    Serial.println("📸 Photo sent to face server.");

    // Step 3: Wait for response
    photoServerResponse = "";
    waitingPhotoResponse = true;
    unsigned long start = millis();

    while (waitingPhotoResponse) {
        faceClient.loop();  // keep websocket alive

        if (millis() - start > timeout) {
            waitingPhotoResponse = false;
            return "ERROR: Timeout";
        }
    }

    return photoServerResponse;
}


// ================= Loop =================
/*
void loop() {
  if (millis() - lastCardRead < CARD_READ_COOLDOWN) {
  Serial.println("Cooldown active");
  return;
}
if (!mfrc522.PICC_IsNewCardPresent()) {
  //Serial.println("No new card present");
  return;
}
if (!mfrc522.PICC_ReadCardSerial()) {
  Serial.println("Failed to read card serial");
  //return;
}
  lastCardRead = millis();
  //String uidStr = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  String uidStr = "10.00.00.10";
  Serial.println("Card UID: " + uidStr);

  String cardResponse;

  if (waitingForNewCard) {
    cardResponse = sendCardRequestAndWait("{\"type\":\"add_card\",\"uid\":\"" + uidStr + "\"}");
    Serial.println("📩 Card server response: " + cardResponse);
    waitingForNewCard = false;
  } else if (uidStr == "MASTERUID") {
    Serial.println("🔑 Master card detected → next card will be added");
    waitingForNewCard = true;
  } else {
    cardResponse = sendCardRequestAndWait("{\"type\":\"check_card\",\"uid\":\"" + uidStr + "\"}");
    Serial.println("📩 Card server response: " + cardResponse);

    if (cardResponse.indexOf("\"status\":\"ok\"") > 0) {
      Serial.println("✅ Card authorized → capturing photo");
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        faceClient.sendBIN(fb->buf, fb->len);  // send to face server
        esp_camera_fb_return(fb);
      }
      String faceResponse = sendFaceRequestAndWait("{\"type\":\"check_face\"}");
      Serial.println("📩 Face server response: " + faceResponse);
    } else {
      Serial.println("⛔ Card denied");
    }
  }

  mfrc522.PICC_HaltA();
}*/

void loop() {
  // Wait for cooldown
  faceClient.loop();
  
  // ===== Only take photo for any card =====
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
  } else {
    sendPhoto(fb, "add");
  }
  delay(10000);
  if (!fb) {
    Serial.println("Camera capture failed");
  } else {
    sendPhoto(fb, "check");
  }
  esp_camera_fb_return(fb);
  delay(100000);


}



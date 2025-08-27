#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SoftwareSerial.h>


// ---- OLED config ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_TIMEOUT = 10000;
// ---- RFID config ----
#define SS_PIN 10
#define RST_PIN 9
MFRC522 rfid(SS_PIN, RST_PIN);

// ---- Config ----
byte masterUID[4] = { 0x63, 0xA3, 0x44, 0x16 };
const unsigned long MASTER_DOUBLE_WINDOW = 3000;
const unsigned long CLEAR_COOLDOWN = 1500;

// ---- State ----
bool enrollMode = false;
unsigned long lastMasterScan = 0;
unsigned long clearedAt = 0;

// ---- EEPROM ----
const int EEPROM_SIZE = 1024;
const int UID_LEN = 4;
const int EEPROM_BASE = 0;

bool compareUID(const byte *a, const byte *b) {
  for (byte i = 0; i < UID_LEN; i++)
    if (a[i] != b[i]) return false;
  return true;
}

void saveUIDToEEPROM(const byte *uid, int addr) {
  for (byte i = 0; i < UID_LEN; i++) EEPROM.update(addr + i, uid[i]);
}

void readUIDFromEEPROM(byte *uid, int addr) {
  for (byte i = 0; i < UID_LEN; i++) uid[i] = EEPROM.read(addr + i);
}

bool isEmptySlotAddr(int addr) {
  byte u[UID_LEN];
  readUIDFromEEPROM(u, addr);
  for (byte i = 0; i < UID_LEN; i++)
    if (u[i] != 0xFF) return false;
  return true;
}

int findFreeSlot() {
  for (int addr = EEPROM_BASE; addr <= EEPROM_SIZE - UID_LEN; addr += UID_LEN) {
    if (isEmptySlotAddr(addr)) return addr;
  }
  return -1;
}

bool isUIDAuthorized(const byte *uid) {
  byte stored[UID_LEN];
  for (int addr = EEPROM_BASE; addr <= EEPROM_SIZE - UID_LEN; addr += UID_LEN) {
    readUIDFromEEPROM(stored, addr);
    bool empty = true;
    for (byte i = 0; i < UID_LEN; i++)
      if (stored[i] != 0xFF) {
        empty = false;
        break;
      }
    if (empty) return false;
    if (compareUID(uid, stored)) return true;
  }
  return false;
}

void clearEEPROMAll() {
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.update(i, 0xFF);
}

void showMessage(const char *line1, const char *line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  if (line2[0] != '\0') {
    display.setCursor(0, 16);
    display.println(line2);
  }
  lastDisplayUpdate = millis();
  display.display();
}

/*
String sendToESP32(const char *msg, unsigned long timeout = 6000) {
  Serial.println(msg);   // send command
  unsigned long start = millis();
  String reply = Serial.readStringUntil('\n');
  reply.trim();
  return reply;
}
*/

String sendToESP32(const char *msg, unsigned long timeout = 6000) {
  Serial.flush();                            // clear outgoing buffer
  while (Serial.available()) Serial.read();  // flush any old incoming data

  Serial.println(msg);  // send command

  unsigned long start = millis();
  String reply = "";

  while (millis() - start < timeout) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;  // end of response
      reply += c;
    }
  }

  reply.trim();
  return reply;
}




// ---- Setup ----
void setup() {

  Serial.begin(9600);


  Wire.begin();  // A4=SDA, A5=SCL on Nano
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    digitalWrite(13, HIGH);
    delay(2500);
    digitalWrite(13, LOW);
    for (;;);
  }
  display.clearDisplay();
  display.display();

  SPI.begin();
  rfid.PCD_Init();



  showMessage("System Ready", "Scan card...");
}

// ---- Loop ----
void loop() {

  if (millis() - lastDisplayUpdate > DISPLAY_TIMEOUT) {
    showMessage("System Ready", "Scan card...");
  }

  if ((clearedAt && (millis() - clearedAt < CLEAR_COOLDOWN))) {
    return;
  }

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  byte *uid = rfid.uid.uidByte;
  byte sz = rfid.uid.size;

  // Master card handling
  if (compareUID(uid, masterUID)) {
    unsigned long now = millis();
    if (now - lastMasterScan < MASTER_DOUBLE_WINDOW) {
      showMessage("Master Double", "Clearing EEPROM...");
      clearEEPROMAll();
      enrollMode = false;
      clearedAt = millis();
      showMessage("EEPROM Cleared", "Scan Master again");
    } else {
      enrollMode = true;
      clearedAt = 0;
      showMessage("Master OK", "Next card enroll");
    }
    lastMasterScan = now;
  }
  // Enroll path
  else if (enrollMode) {
    int addr = findFreeSlot();
    if (addr >= 0) {
      saveUIDToEEPROM(uid, addr);
      showMessage("Card Enrolled", "Stored in EEPROM");
      String resp = sendToESP32("register");
      showMessage("Sent register", resp.c_str());
    } else {
      showMessage("EEPROM FULL");
    }
    enrollMode = false;
  }
  // Normal check
  else {
    if (isUIDAuthorized(uid)) {
      showMessage("Card Access Granted, Wait for Camera");
      String resp = sendToESP32("check");
      showMessage("Sent check", resp.c_str());
    } else {
      showMessage("Access Denied");
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

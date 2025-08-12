#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

// Pin configuration based on your wiring
#define RST_PIN    2     // RC522 RST -> ESP32 IO2
#define SS_PIN     13    // RC522 SDA -> ESP32 IO13
#define FLASH_PIN  4     // ESP32-CAM flash LED (GPIO 4)

#define EEPROM_SIZE 16   // enough for 4 bytes UID + status

// Create MFRC522 instance
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Master tag UID (replace with your master card’s UID)
byte masterUID[4] = {0x63, 0xA3, 0x44, 0x16};

// Trusted tag UID storage
byte trustedUID[4];
bool trustedSet = false;
bool waitingForTrusted = false;

void saveTrustedUID(byte *uid) {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(i, uid[i]);
    trustedUID[i] = uid[i];
  }
  EEPROM.write(4, 1); // Mark as set
  EEPROM.commit();
  trustedSet = true;
}

bool readTrustedUID() {
  if (EEPROM.read(4) == 1) {
    for (int i = 0; i < 4; i++) {
      trustedUID[i] = EEPROM.read(i);
    }
    return true;
  }
  return false;
}

bool compareUID(byte *uid1, byte *uid2) {
  for (byte i = 0; i < 4; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
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

  // Load any saved trusted UID
  trustedSet = readTrustedUID();

  SPI.begin(14, 12, 15); // SCK=14, MISO=12, MOSI=15
  mfrc522.PCD_Init();

  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  Serial.println("Place your card near the reader...");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print("Card UID:");
  printUID(mfrc522.uid.uidByte, mfrc522.uid.size);

  if (waitingForTrusted) {
    // Save this card as trusted
    saveTrustedUID(mfrc522.uid.uidByte);
    Serial.println("✅ New trusted UID saved!");
    waitingForTrusted = false;
  }
  else if (compareUID(mfrc522.uid.uidByte, masterUID)) {
    Serial.println("🔑 Master card detected. Present next card to set as trusted.");
    waitingForTrusted = true;
  }
  else if (trustedSet && compareUID(mfrc522.uid.uidByte, trustedUID)) {
    Serial.println("✅ Trusted card detected. Flash ON!");
    digitalWrite(FLASH_PIN, HIGH);
    delay(1000);
    digitalWrite(FLASH_PIN, LOW);
  }
  else {
    Serial.println("❌ Unknown card.");
  }

  mfrc522.PICC_HaltA();
}

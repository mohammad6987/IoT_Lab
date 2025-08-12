#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

#define RST_PIN    13     // RC522 RST -> ESP32 IO2
#define SS_PIN     2    // RC522 SDA -> ESP32 IO13
#define FLASH_PIN  4     // ESP32-CAM flash LED (GPIO 4)

// EEPROM config
#define EEPROM_SIZE 4096
#define CARD_SIZE   12    // 4 bytes UID + 8 bytes password
#define MAX_CARDS   (EEPROM_SIZE / CARD_SIZE)

// MFRC522 instance
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Master card UID (change this to yours)
byte masterUID[4] = {0x63, 0xA3, 0x44, 0x16};

bool waitingForNewCard = false;

void saveCardToEEPROM(byte *uid, const char *password) {
  // Find empty slot
  for (int slot = 0; slot < MAX_CARDS; slot++) {
    int addr = slot * CARD_SIZE;
    if (EEPROM.read(addr) == 0xFF) { // empty
      // Save UID
      for (int i = 0; i < 4; i++) {
        EEPROM.write(addr + i, uid[i]);
      }
      // Save password
      for (int i = 0; i < 8; i++) {
        EEPROM.write(addr + 4 + i, password[i]);
      }
      EEPROM.commit();
      Serial.print("✅ Card saved in slot ");
      Serial.println(slot);
      return;
    }
  }
  Serial.println("⚠️ EEPROM full! Cannot save new card.");
}

bool findCardInEEPROM(byte *uid, char *passwordOut) {
  for (int slot = 0; slot < MAX_CARDS; slot++) {
    int addr = slot * CARD_SIZE;
    if (EEPROM.read(addr) == 0xFF) continue; // empty slot

    bool match = true;
    for (int i = 0; i < 4; i++) {
      if (EEPROM.read(addr + i) != uid[i]) {
        match = false;
        break;
      }
    }
    if (match) {
      // Copy password
      for (int i = 0; i < 8; i++) {
        passwordOut[i] = EEPROM.read(addr + 4 + i);
      }
      passwordOut[8] = '\0';
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

  // Initialize SPI and RFID
  SPI.begin(14, 12, 15);
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

  if (waitingForNewCard) {
    saveCardToEEPROM(mfrc522.uid.uidByte, "00000000");
    waitingForNewCard = false;
  }
  else if (memcmp(mfrc522.uid.uidByte, masterUID, 4) == 0) {
    Serial.println("🔑 Master card detected. Present next card to add.");
    waitingForNewCard = true;
  }
  else {
    char password[9];
    if (findCardInEEPROM(mfrc522.uid.uidByte, password)) {
      Serial.print("✅ Trusted card detected. Password: ");
      Serial.println(password);
      digitalWrite(FLASH_PIN, HIGH);
      delay(1000);
      digitalWrite(FLASH_PIN, LOW);
    } else {
      Serial.println("❌ Unknown card.");
    }
  }

  mfrc522.PICC_HaltA();
}

#include <SPI.h>
#include <LoRa.h>

// ============ CONFIGURATION ============
#define IBUS_TX_PIN 17   // Serial1 TX to Flight Controller
#define CHANNELS 14      // iBus supports 14 channels
#define FAILSAFE_MS 1000 // Time before failsafe activates

// ============ LORA PINS (Ra-02 / SX1278) ============
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26

// ============ LORA SETTINGS (Must match TX) ============
#define LORA_FREQUENCY 433E6 // LoRa Frequency (e.g., 433E6, 868E6, 915E6)
#define LORA_SF 7 // Spreading Factor (Range 6-12). Higher SF = longer range, slower data rate.
#define LORA_BANDWIDTH 125E3 // LoRa Bandwidth 125 and 500

// ============ DATA STRUCTURE (Matches TX) ============
typedef struct {
  uint16_t ch[16]; // Receiving 16, but iBus sends 14
  uint8_t checksum;
} __attribute__((packed)) CompressedRCData;

// ============ GLOBAL VARIABLES ============
HardwareSerial IBusSerial(1); // Use UART1
volatile CompressedRCData receivedData;
volatile bool newDataAvailable = false;
volatile unsigned long lastReceiveTime = 0;

// Current Channel States
uint16_t channels[CHANNELS] = {
  1500,1500,1000,1500, // R, P, T, Y
  1000,1000,1000,1000,1000,1000,1000,1000,1000,1000
};

// ============ iBUS PROTOCOL ============
uint16_t ibus_checksum(uint8_t* buf, uint8_t len) {
  uint16_t sum = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) sum -= buf[i];
  return sum;
}

void sendIBusPacket() {
  uint8_t buf[32];
  buf[0] = 0x20; // Header
  buf[1] = 0x40; // Header
  
  for (int i = 0; i < 14; i++) {
    buf[2 + i * 2] = channels[i] & 0xFF;
    buf[3 + i * 2] = channels[i] >> 8;
  }
  
  uint16_t sum = ibus_checksum(buf, 30);
  buf[30] = sum & 0xFF;
  buf[31] = sum >> 8;
  
  IBusSerial.write(buf, 32);
}

// ============ LORA CALLBACK ============
void onReceive(int packetSize) {
  if (packetSize == sizeof(CompressedRCData)) {
    CompressedRCData temp;
    LoRa.readBytes((uint8_t*)&temp, sizeof(temp));
    
    // Verify checksum
    uint8_t calc_checksum = 0;
    for (int i = 0; i < 16; i++) {
      calc_checksum ^= (temp.ch[i] & 0xFF);
      calc_checksum ^= (temp.ch[i] >> 8);
    }
    
    if (calc_checksum == temp.checksum) {
      memcpy((void*)&receivedData, &temp, sizeof(temp));
      newDataAvailable = true;
      lastReceiveTime = millis();
    }
  }
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  
  // iBUS Serial
  IBusSerial.begin(115200, SERIAL_8N1, -1, IBUS_TX_PIN);
  
  // LoRa Init
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed!");
    while(1);
  }
  
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.onReceive(onReceive);
  LoRa.receive(); // Start listening
  
  Serial.println("LoRa iBUS RX Ready");
}

// ============ LOOP ============
void loop() {
  unsigned long now = millis();
  
  // 1. Process New Data
  if (newDataAvailable) {
    noInterrupts();
    // Update active channels from received data
    // Map data structure ch[0-15] to iBus channels
    // Note: My iBUS code assumes Standard mapping (AETR or TAER dependent on your FC)
    // Your TX sends: 0:Roll, 1:Pitch, 2:Thr, 3:Yaw
    for(int i=0; i<14; i++) {
      channels[i] = receivedData.ch[i];
    }
    newDataAvailable = false;
    interrupts();
  }
  
  // 2. Failsafe Check
  if (now - lastReceiveTime > FAILSAFE_MS) {
    channels[2] = 1000; // Throttle Channel -> LOW
    // You might want to set other channels to center/neutral here
  }
  
  // 3. Output to FC (every loop ~10ms delay for stability)
  sendIBusPacket();
  delay(10); 
}
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <LoRa.h>
#include <BleGamepad.h>

// ============ CONFIGURATION ============
#define UPDATE_RATE_HZ 50   
#define UPDATE_INTERVAL (1000 / UPDATE_RATE_HZ)

// ============ LORA PINS (Ra-02 / SX1278) ============
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26

// ============ LORA SETTINGS ============
#define LORA_FREQUENCY 433E6 // LoRa Frequency (e.g., 433E6, 868E6, 915E6)
#define LORA_SF 7 // Spreading Factor (Range 6-12). Higher SF = longer range, slower data rate.
#define LORA_TX_POWER 20 // Transmit Power (Range 2-20 dBm). Max is 20, but 18 is common for high power.
#define LORA_BANDWIDTH 125E3 // LoRa Bandwidth 125 and 500

// PPM Input Pins
#define PPM_PIN_CH1 15   // Roll
#define PPM_PIN_CH2 2   // Pitch
#define PPM_PIN_CH3 0  // Throttle
#define PPM_PIN_CH4 4  // Yaw
#define PPM_PIN_CH5 16  // Physical Knob or Switch 1
#define PPM_PIN_CH6 17  // Physical Knob or Switch 2

// WiFi AP Configuration
const char* ssid = "Channal_LoRa";
const char* password = "12345678";

// ============ GLOBAL OBJECTS ============
WebServer server(80);
BleGamepad bleGamepad("RC_Controller_LoRa", "ESP32", 100);

// ============ CHANNEL DATA ============
volatile uint16_t channels[16] = {1500}; 
uint16_t virtualChannels[10] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};

// PPM timing
volatile unsigned long ppmRisingTime[6] = {0};
volatile uint16_t ppmPulseWidth[6] = {1500};

// Calibration
uint16_t calibratedCenters[4] = {1500, 1500, 1500, 1500};
#define DEADZONE 25

// ============ DATA STRUCTURE (Matches RX) ============
typedef struct {
  uint16_t ch[16];
  uint8_t checksum;
} __attribute__((packed)) CompressedRCData;

// ============ PPM INTERRUPT HANDLERS ============
void IRAM_ATTR handlePPM(uint8_t ch, uint8_t pin) {
  if (digitalRead(pin) == HIGH) {
    ppmRisingTime[ch] = micros();
  } else {
    unsigned long w = micros() - ppmRisingTime[ch];
    if (w >= 900 && w <= 2100) ppmPulseWidth[ch] = w;
  }
}

// Separate wrappers for attachInterrupt
void IRAM_ATTR handlePPM_CH1() { handlePPM(0, PPM_PIN_CH1); }
void IRAM_ATTR handlePPM_CH2() { handlePPM(1, PPM_PIN_CH2); }
void IRAM_ATTR handlePPM_CH3() { handlePPM(2, PPM_PIN_CH3); }
void IRAM_ATTR handlePPM_CH4() { handlePPM(3, PPM_PIN_CH4); }
void IRAM_ATTR handlePPM_CH5() { handlePPM(4, PPM_PIN_CH5); }
void IRAM_ATTR handlePPM_CH6() { handlePPM(5, PPM_PIN_CH6); }

void setupPPM() {
  const uint8_t pins[] = {PPM_PIN_CH1, PPM_PIN_CH2, PPM_PIN_CH3, PPM_PIN_CH4, PPM_PIN_CH5, PPM_PIN_CH6};
  void (*handlers[])() = {handlePPM_CH1, handlePPM_CH2, handlePPM_CH3, handlePPM_CH4, handlePPM_CH5, handlePPM_CH6};
  
  for(int i = 0; i < 6; i++) {
    pinMode(pins[i], INPUT);
    attachInterrupt(digitalPinToInterrupt(pins[i]), handlers[i], CHANGE);
  }
}

void readPPMChannels() {
  noInterrupts();
  for (int i = 0; i < 6; i++) channels[i] = ppmPulseWidth[i];
  for (int i = 0; i < 10; i++) channels[i + 6] = virtualChannels[i];
  interrupts();
}

// ============ CALIBRATION ============
void calibrateJoysticks() {
  Serial.println("Calibrating... Keep sticks centered!");
  delay(500);
  long sum[4] = {0};
  for(int i = 0; i < 50; i++) {
    noInterrupts();
    for(int j = 0; j < 4; j++) sum[j] += ppmPulseWidth[j];
    interrupts();
    delay(10);
  }
  for(int i = 0; i < 4; i++) {
    calibratedCenters[i] = sum[i] / 50;
  }
  Serial.println("Calibration complete!");
}

// ============ LORA SEND FUNCTION ============
void sendLoRa() {
  CompressedRCData data;
  for (int i = 0; i < 16; i++) data.ch[i] = channels[i];
  
  // Calculate Checksum
  data.checksum = 0;
  for (int i = 0; i < 16; i++) {
    data.checksum ^= (data.ch[i] & 0xFF);
    data.checksum ^= (data.ch[i] >> 8);
  }
  
  // Send Packet (Async to not block BLE/WiFi)
  if(LoRa.beginPacket()) {
    LoRa.write((uint8_t*)&data, sizeof(data));
    LoRa.endPacket(true); // true = Async (non-blocking)
  }
}

// ============ BLE GAMEPAD OUTPUT ============
void sendBLEGamepad() {
  if (!bleGamepad.isConnected()) return;
  
  uint16_t ch[4];
  for(int i = 0; i < 4; i++) {
    if (abs((int)channels[i] - (int)calibratedCenters[i]) < DEADZONE) {
      ch[i] = calibratedCenters[i];
    } else {
      ch[i] = channels[i];
    }
  }
  
  auto mapCh = [](uint16_t val, uint16_t minV, uint16_t maxV) {
    val = constrain(val, minV, maxV);
    return (val - minV) * 32767 / (maxV - minV);
  };
  
  bleGamepad.setAxes(
    mapCh(ch[0], 1000, 2000), mapCh(ch[1], 1000, 2000), 
    mapCh(ch[2], 1000, 2000), mapCh(ch[3], 1000, 2000),
    mapCh(channels[4], 1000, 2000), mapCh(channels[5], 1000, 2000)
  );
  
  for(int i = 0; i < 10; i++) {
    if (virtualChannels[i] >= 1750) bleGamepad.press(i + 1);
    else if (virtualChannels[i] >= 1250) bleGamepad.release(i + 1);
    else bleGamepad.release(i + 1);
  }
  bleGamepad.sendReport();
}

// ============ WEB SERVER (Minified) ============
const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoRa RC Config</title><style>
body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}
h2{color:#4CAF50}.info{padding:15px;background:#2a2a2a;border-radius:8px;margin:15px 0}
.btn{flex:1;padding:15px;border:none;border-radius:5px;cursor:pointer;margin:2px}
.btn.on{background:#4CAF50}.btn:not(.on){background:#444;color:#aaa}
.sw{display:flex;margin-top:5px}</style></head><body>
<h2>LoRa RC TX</h2><div class="info">Status: <span id="s">Connecting...</span></div>
<div id="vs"></div><script>
let sw=Array(10).fill(1000);
for(let i=0;i<10;i++){
  let h='<div><b>CH'+(i+7)+'</b><div class="sw">';
  [1000,1500,2000].forEach(v=>{h+=`<button class="btn" onclick="s(${i},${v})" data-c="${i}" data-v="${v}">${v}</button>`});
  h+='</div></div>';document.getElementById('vs').innerHTML+=h;
}
function s(c,v){fetch(`/s?c=${c}&v=${v}`).then(u);}
function u(){
  fetch('/d').then(r=>r.json()).then(d=>{
    document.getElementById('s').innerText = d.ble ? "BLE Connected" : "BLE Disconnected";
    d.v.forEach((v,i)=>{
      document.querySelectorAll(`.btn[data-c="${i}"]`).forEach(b=>b.classList.toggle('on', b.dataset.v==v));
    });
  });
}
setInterval(u,1000);u();
</script></body></html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", FPSTR(html)); }
void handleSet() {
  if (server.hasArg("c") && server.hasArg("v")) {
    int c = server.arg("c").toInt();
    int v = server.arg("v").toInt();
    if (c >= 0 && c < 10) virtualChannels[c] = v;
    server.send(200, "text/plain", "OK");
  } else server.send(400, "text/plain", "Bad");
}
void handleData() {
  String j = "{\"v\":[";
  for (int i = 0; i < 10; i++) { j += virtualChannels[i]; if(i<9) j+=","; }
  j += "],\"ble\":" + String(bleGamepad.isConnected() ? "true" : "false") + "}";
  server.send(200, "application/json", j);
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- LoRa PPM TX ---");
  
  setupPPM();
  delay(100);
  calibrateJoysticks();
  
  // Init LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed!");
    while(1);
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setTxPower(LORA_TX_POWER);
  Serial.println("LoRa Active");

  // BLE
  BleGamepadConfiguration cfg;
  cfg.setAutoReport(false);
  cfg.setControllerType(CONTROLLER_TYPE_JOYSTICK);
  cfg.setButtonCount(10);
  bleGamepad.begin(&cfg);
  
  // WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  server.on("/", handleRoot);
  server.on("/s", handleSet);
  server.on("/d", handleData);
  server.begin();
  Serial.println("Web/BLE Ready");
}

// ============ LOOP ============
unsigned long lastUpdate = 0;

void loop() {
  server.handleClient();
  
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;
    
    // 1. Read Inputs
    readPPMChannels();
    
    // 2. Send Radio (LoRa)
    sendLoRa();
    
    // 3. Send Simulator (BLE)
    sendBLEGamepad();
  }
}
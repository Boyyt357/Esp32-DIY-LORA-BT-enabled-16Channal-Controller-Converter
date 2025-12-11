# **DIY LoRa RC Controller/Converter (16 Channels, BLE, iBus)**

This project provides a robust, long-range 16-channel control system built on two ESP32 modules and LoRa transceivers (like the Ra-02/SX1278). It converts a standard RC transmitter's PPM output into both a long-range LoRa signal for drones/vehicles and a local Bluetooth Gamepad for simulators.

## **🚀 Key Features**

* **Long-Range LoRa:** Utilizes LoRa for reliable, low-power, and long-distance communication (up to 915MHz).  
* **16 Channels:** Supports 6 physical channels (PPM) and 10 virtual switch channels.  
* **Dual Output (TX Unit):** Simultaneously transmits via LoRa and acts as a **Bluetooth Gamepad**.  
* **iBus Converter (RX Unit):** Converts received LoRa data into the standard **iBus protocol** for flight controllers.  
* **Web Configuration:** Built-in Wi-Fi Access Point and Web Server for managing virtual switches (CH7-CH16).

## **⚙️ Component Architecture**

### **1\. Transmitter (TX Unit \- Controller)**

The TX unit is the control hub, processing physical and virtual inputs, and broadcasting on two separate protocols.

| Feature | Input/Output | Details |
| :---- | :---- | :---- |
| **Input** | 6x PPM Signals | Reads Roll, Pitch, Throttle, Yaw, plus 2 Aux channels via interrupts. |
| **Virtual Input** | Web Server | Manages 10 virtual switch channels (CH7-CH16) for functions like arming, modes, etc. |
| **Long-Range TX** | LoRa Module | Sends compressed 16-channel packets at 50Hz. |
| **Local Output** | Bluetooth (BLE) | Emulates a joystick for simulators/PC control. |
| **Setup** | Wi-Fi AP | Hosts a simple web interface for channel configuration. |

### **2\. Receiver (RX Unit \- Vehicle)**

The RX unit acts as the flight controller's dedicated receiver.

| Feature | Details |
| :---- | :---- |
| **Input** | LoRa Module |
| **Failsafe** | Throttle Cut |
| **Output** | iBus Protocol |

## **🪢 Hardware and Wiring**

### **1\. LoRa Module Wiring (TX and RX Units)**

Both the TX and RX units use the same SPI pin configuration to connect to the LoRa module (e.g., Ra-02).

| ESP32 Pin | LoRa Function |
| :---- | :---- |
| **GPIO 5** | SCK (SPI Clock) |
| **GPIO 19** | MISO (SPI Master In, Slave Out) |
| **GPIO 27** | MOSI (SPI Master Out, Slave In) |
| **GPIO 18** | SS (SPI Chip Select) |
| **GPIO 14** | RST (Reset) |
| **GPIO 26** | DIO0 (Interrupt Line) |

### **2\. TX Unit: PPM Input Pins**

The 6 PPM channels from your RC transmitter's trainer port connect to these specific GPIO pins:

| PPM Channel | Function | ESP32 GPIO Pin |
| :---- | :---- | :---- |
| CH1 | Roll | **15** |
| CH2 | Pitch | **2** |
| CH3 | Throttle | **0** |
| CH4 | Yaw | **4** |
| CH5 | Aux 1 | **16** |
| CH6 | Aux 2 | **17** |

### **3\. RX Unit: iBus Output**

The iBus serial signal for the flight controller is outputted on the following pin:

| Signal | ESP32 GPIO Pin | Connection to Flight Controller |
| :---- | :---- | :---- |
| iBus Signal Out | **17** (UART1 TX) | Flight Controller's RX Pin |

## **📡 LoRa and Wi-Fi Configuration**

The following parameters must match exactly between the TX and RX units for LoRa communication to work.

| Parameter | TX (TX.ino) Value | RX (RX.ino) Value | Notes |
| :---- | :---- | :---- | :---- |
| **Frequency** | 433E6 | 433E6 | Check local regulations (e.g., 868E6 or 915E6 may be required). |
| **Spreading Factor (SF)** | 7 | 7 | Higher SF \= longer range, lower data rate. |
| **Bandwidth (BW)** | 125E3 | 125E3 |  |
| **TX Power** | 20 | N/A | TX only. Sets transmission power in dBm (max $20$). |

### **Wi-Fi Access Point (TX Unit Only)**

* **SSID:** Channal\_LoRa  
* **Password:** 12345678

## **💻 Usage and Web Interface**

1. **Calibration:** When the TX unit boots, it automatically runs calibrateJoysticks() to find the center positions for Roll, Pitch, Throttle, and Yaw. Keep the sticks centered during startup.  
2. **Connect BLE:** The TX unit advertises itself as an "RC\_Controller\_LoRa" Bluetooth Gamepad. Pair it with your PC or simulator machine.  
3. **Access Web UI:** Connect a device (phone/PC) to the Wi-Fi AP (Channal\_LoRa). Navigate to the ESP32's IP address (typically 192.168.4.1).  
4. **Control Virtual Channels:** Use the web page to set the state of the 10 virtual switches (CH7 through CH16) to $1000$ (Low), $1500$ (Mid), or $2000$ (High). These values are sent over LoRa to the drone and mapped to buttons 1-10 on the BLE Gamepad.

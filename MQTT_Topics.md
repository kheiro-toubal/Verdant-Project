# 📘 MQTT Commands Reference

This document provides a comprehensive reference for all MQTT topics and commands used in the Smart Agriculture System.

## Topic Structure

All topics follow a hierarchical structure with the following pattern:
```
wh/{WAREHOUSE_ID}/[device_type]/[function]
```

Where:
- `{WAREHOUSE_ID}` is the unique UUID for each warehouse (assigned by the cloud)
- `[device_type]` is typically 'esp' or 'pi'
- `[function]` indicates the purpose of the message

Some topics include the MAC address of the device (either Pi or ESP).

---

## Device Identification Topics

### 🔧 Raspberry Pi Identification

| Topic                               | Direction      | Format                                   | Description                        |
|--------------------------------------|---------------|------------------------------------------|------------------------------------|
| `wh/pi/id/request`                   | Pi → Cloud    | `{"mac": "<pi_mac>"}`                    | Pi requests a warehouseId using its MAC |
| `wh/pi/{pi_mac}/id/response`         | Cloud → Pi    | `{"warehouseId": "<warehouse_id>"}`      | Cloud assigns warehouseId to Pi    |

### 🔧 ESP8266 Identification

| Topic                                  | Direction       | Format                                                  | Description                            |
|-----------------------------------------|-----------------|---------------------------------------------------------|----------------------------------------|
| `esp/id/request`                        | ESP → Pi        | `{"mac": "<esp_mac>"}`                                  | ESP requests a cellId using its MAC    |
| `wh/esp/id/request`                     | Pi → Cloud      | `{"mac": "<esp_mac>", "warehouseId": "<warehouse_id>"}` | Pi forwards ESP request to cloud       |
| `wh/{WAREHOUSE_ID}/esp/id/response`     | Cloud → Pi      | `{"cellId": "<cell_id>", "mac": "<esp_mac>"}`           | Cloud assigns cellId to ESP            |
| `esp/{esp_mac}/id/response`             | Pi → ESP        | `{"cellId": "<cell_id>", "warehouseId": "<warehouse_id>"}` | Pi forwards cellId and warehouseId to ESP |

---

## Data Topics

### 📤 Sensor Data

| Topic                           | Direction  | Format           | Description                    |
|----------------------------------|------------|------------------|--------------------------------|
| `wh/esp/data`                    | ESP → Pi   | JSON (see below) | ESP publishes sensor data      |
| `wh/{WAREHOUSE_ID}/esp/data`     | Pi → Cloud | JSON (see below) | Pi forwards sensor data to cloud |

**Sensor Data Format:**
```json
{
  "id": "<cell_id>",
  "temp": 22.3,
  "hum": 60.1,
  "ldr": 130,
  "co2": 800,
  "soil": 350,
  "water": 1,
  "air_pump_state": 1,
  "water_pump_state": 0,
  "light_state": 1,
  "heater_state": 0,
  "timestamp": "2025-12-12T10:10:10.100Z"
}
```

### ⏱️ Consumption Data

| Topic                              | Direction  | Format            | Description                         |
|-------------------------------------|------------|-------------------|-------------------------------------|
| `wh/esp/consumption`                | ESP → Pi   | JSON (see below)  | ESP publishes consumption data      |
| `wh/{WAREHOUSE_ID}/esp/consumption` | Pi → Cloud | JSON (see below)  | Pi forwards consumption data to cloud |

**Consumption Data Format:**
```json
{
  "id": "<cell_id>",
  "water_pump": 1.25,  // liters
  "air_pump": 0.75,    // liters
  "light": 15.0,       // watt-minutes
  "heater": 30.0       // watt-minutes
}
```

---

## Control Topics

### 📥 Actuator Commands

| Topic                              | Direction      | Format           | Description                    |
|-------------------------------------|---------------|------------------|--------------------------------|
| `wh/{WAREHOUSE_ID}/esp/command`     | Cloud → Pi    | JSON (see below) | Cloud sends actuator commands  |
| `esp/commands`                      | Pi → ESP      | JSON (see below) | Pi forwards commands to ESP    |

**Command Format:**
```json
{
  "id": "<cell_id or 'all'>",
  "air_pump": 1,
  "water_pump": 0,
  "light": 1,
  "heater": 0
}
```

### 🔄 Operation Mode Commands

| Topic                                 | Direction   | Format (see below) | Description                |
|----------------------------------------|-------------|--------------------|----------------------------|
| `wh/{WAREHOUSE_ID}/esp/mode/change`    | Cloud → Pi  | JSON               | Cloud sends mode change    |
| `esp/mode/change`                      | Pi → ESP    | JSON               | Pi forwards mode change    |

**Mode Change Format:**
```json
{
  "id": "<cell_id or 'all'>",
  "mode": "auto"       // "auto" or "manual"
}
```

---

## Summary Table of All Topics

| Topic                                         | Direction               | Purpose/Notes                         |
|------------------------------------------------|------------------------|---------------------------------------|
| wh/pi/id/request                              | Pi → Cloud             | Request warehouse ID                  |
| wh/pi/{pi_mac}/id/response                    | Cloud → Pi             | Response with warehouse ID            |
| esp/id/request                                | ESP → Pi               | ESP requests cell ID                  |
| wh/esp/id/request                             | Pi → Cloud             | Forward ESP ID request                |
| wh/{WAREHOUSE_ID}/esp/id/response             | Cloud → Pi             | Response with cell ID                 |
| esp/{esp_mac}/id/response                     | Pi → ESP               | Response to ESP with cell ID          |
| wh/esp/data                                   | ESP → Pi               | Sensor data from ESP                  |
| wh/{WAREHOUSE_ID}/esp/data                    | Pi → Cloud             | Sensor data to cloud                  |
| wh/esp/consumption                            | ESP → Pi               | Consumption data from ESP             |
| wh/{WAREHOUSE_ID}/esp/consumption             | Pi → Cloud             | Consumption data to cloud             |
| wh/{WAREHOUSE_ID}/esp/command                 | Cloud → Pi             | Commands from cloud                   |
| esp/commands                                  | Pi → ESP               | Commands to ESP                       |
| wh/{WAREHOUSE_ID}/esp/mode/change             | Cloud → Pi             | Mode change from cloud                |
| esp/mode/change                               | Pi → ESP               | Mode change to ESP                    |

---

## Notes

- `{WAREHOUSE_ID}` is assigned by the cloud and stored on the Pi.
- `{pi_mac}` and `{esp_mac}` are device MAC addresses.
- All topic routing and forwarding logic is in Pi_Script.py; see that file for implementation details.

---

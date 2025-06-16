#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

const char* ssid = "4.9G";
const char* password = "Ta56Gaiu7";
const char* mqtt_server = "raspberrypi.local";

WiFiClient espClient;
PubSubClient client(espClient);

// NTP Client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); // GMT+1 (3600 seconds offset), Update every minute

#define EEPROM_SIZE 150  // Increased size to store both cellId and warehouseId
String cellId = "";
String warehouseId = "";
String espMAC = "";
const String commandTopic = "esp/commands";

// ID Request with Exponential Backoff
unsigned long lastIDRequest = 0;
int idRequestAttempts = 0;
bool waitingForID = false;
const int MAX_ID_ATTEMPTS = 10;
const unsigned long BASE_RETRY_DELAY = 2000; // 2 seconds base delay

#define AIR_PUMP D1
#define WATER_PUMP D2
#define LIGHT D3
#define HEATER D5
#define RESET_BUTTON D6  // Reset button pin

// Button debouncing
unsigned long lastButtonPress = 0;
bool lastButtonState = HIGH;
const unsigned long DEBOUNCE_DELAY = 50;

String operationMode = "auto";
#define SOIL_THRESHOLD 400
#define LIGHT_THRESHOLD 150
#define CO2_THRESHOLD 700

// Current sensor values (maintained separately for auto control)
int currentSoil = 0, currentLight = 0, currentCO2 = 0;
float currentTemp = 0, currentHum = 0;
int currentWaterLevel = 0;

// Actuator specs
#define WATER_FLOW_RATE 2.5
#define AIR_FLOW_RATE 1.5
#define LIGHT_POWER 10
#define HEATER_POWER 20

// Actuator tracking
unsigned long waterPumpStart = 0, airPumpStart = 0, lightStart = 0, heaterStart = 0;
unsigned long waterPumpTotalTime = 0, airPumpTotalTime = 0, lightTotalTime = 0, heaterTotalTime = 0;
bool waterPumpActive = false, airPumpActive = false, lightActive = false, heaterActive = false;

// Data forwarding timing
unsigned long lastDataForward = 0;
const unsigned long FORWARD_INTERVAL = 5000; // Forward every 5 seconds

void clearEEPROM() {
  Serial.println("[RESET] Clearing EEPROM and resetting device IDs...");
  EEPROM.begin(EEPROM_SIZE);
  
  // Clear EEPROM by writing 0xFF to all addresses
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  
  // Reset ID variables
  cellId = "";
  warehouseId = "";
  
  // Reset ID request state
  idRequestAttempts = 0;
  waitingForID = false;
  lastIDRequest = 0;
  
  Serial.println("[RESET] EEPROM cleared successfully. Device will request new ID assignment.");
  
  // Unsubscribe from command topics and resubscribe to ID response
  client.unsubscribe(commandTopic.c_str());
  String responseTopicMAC = "esp/" + espMAC + "/id/response";
  client.subscribe(responseTopicMAC.c_str());
  
  Serial.println("[RESET] Ready for new ID assignment");
}

void handleResetButton() {
  bool currentButtonState = digitalRead(RESET_BUTTON);
  
  // Check for button press (transition from HIGH to LOW with debouncing)
  if (currentButtonState == LOW && lastButtonState == HIGH && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    lastButtonPress = millis();
    Serial.println("[RESET] Reset button pressed!");
    clearEEPROM();
  }
  
  lastButtonState = currentButtonState;
}

String getISOTimestamp() {
  static unsigned long lastNTPUpdate = 0;
  
  // Only update NTP every 10 minutes, not every call!
  if (millis() - lastNTPUpdate >= 600000) {
    timeClient.update();
    lastNTPUpdate = millis();
  }
  
  unsigned long epochTime = timeClient.getEpochTime();
  time_t rawtime = epochTime;
  struct tm * ptm = gmtime(&rawtime);
  
  char timestamp[30];
  sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d.000+01:00",
          ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
          ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
  
  return String(timestamp);
}

void handleAutoControls() {
  // Auto mode soil moisture control
  if (currentSoil > SOIL_THRESHOLD && !waterPumpActive) {
    waterPumpStart = millis(); 
    waterPumpActive = true; 
    digitalWrite(WATER_PUMP, HIGH);
    Serial.println("[AUTO] Water pump activated - Soil moisture below threshold (" + String(currentSoil) + " < " + String(SOIL_THRESHOLD) + ")");
  } else if (currentSoil <= SOIL_THRESHOLD && waterPumpActive) {
    waterPumpTotalTime += millis() - waterPumpStart; 
    waterPumpActive = false; 
    digitalWrite(WATER_PUMP, LOW);
    Serial.println("[AUTO] Water pump deactivated - Soil moisture adequate (" + String(currentSoil) + " >= " + String(SOIL_THRESHOLD) + ")");
  }

  // Auto mode light control
  if (currentLight < LIGHT_THRESHOLD && !lightActive) {
    lightStart = millis(); 
    lightActive = true; 
    digitalWrite(LIGHT, HIGH);
    Serial.println("[AUTO] Light activated - Ambient light below threshold (" + String(currentLight) + " < " + String(LIGHT_THRESHOLD) + ")");
  } else if (currentLight >= LIGHT_THRESHOLD && lightActive) {
    lightTotalTime += millis() - lightStart; 
    lightActive = false; 
    digitalWrite(LIGHT, LOW);
    Serial.println("[AUTO] Light deactivated - Ambient light adequate (" + String(currentLight) + " >= " + String(LIGHT_THRESHOLD) + ")");
  }

  // Auto mode CO2 control
  if (currentCO2 > CO2_THRESHOLD && !airPumpActive) {
    airPumpStart = millis(); 
    airPumpActive = true; 
    digitalWrite(AIR_PUMP, HIGH);
    Serial.println("[AUTO] Air pump activated - CO2 above threshold (" + String(currentCO2) + " > " + String(CO2_THRESHOLD) + ")");
  } else if (currentCO2 <= CO2_THRESHOLD && airPumpActive) {
    airPumpTotalTime += millis() - airPumpStart; 
    airPumpActive = false; 
    digitalWrite(AIR_PUMP, LOW);
    Serial.println("[AUTO] Air pump deactivated - CO2 at acceptable level (" + String(currentCO2) + " <= " + String(CO2_THRESHOLD) + ")");
  }

  // Auto mode temperature control
  if (currentTemp < 18 && !heaterActive) {
    heaterStart = millis(); 
    heaterActive = true; 
    digitalWrite(HEATER, HIGH);
    Serial.println("[AUTO] Heater activated - Temperature below threshold (" + String(currentTemp) + "°C < 18°C)");
  } else if (currentTemp >= 18 && heaterActive) {
    heaterTotalTime += millis() - heaterStart; 
    heaterActive = false; 
    digitalWrite(HEATER, LOW);
    Serial.println("[AUTO] Heater deactivated - Temperature adequate (" + String(currentTemp) + "°C >= 18°C)");
  }
}

void processIncomingData() {
  String incomingData = Serial.readStringUntil('\n');
  incomingData.trim();
  
  // Handle regular sensor data JSON
  StaticJsonDocument<200> sensorJson;
  if (!deserializeJson(sensorJson, incomingData)) {
    // Update current sensor values for auto control
    currentTemp = sensorJson["temp"];
    currentHum = sensorJson["hum"];
    currentLight = sensorJson["ldr"];
    currentCO2 = sensorJson["co2"];
    currentSoil = sensorJson["soil"];
    currentWaterLevel = sensorJson["water"];
    

  } else {
    // Handle error case
    if (incomingData.indexOf("error") != -1) {
      Serial.println("[DEBUG] Arduino DHT sensor not ready");
    } else {
      Serial.println("[ERROR] Failed to parse sensor data JSON: " + incomingData);
    }
  }
}

void forwardSensorData() {
  // Only publish sensor data if we have a valid ID and it's time to forward
  if (cellId.length() > 0 && millis() - lastDataForward >= FORWARD_INTERVAL) {
    lastDataForward = millis();
    
    StaticJsonDocument<300> jsonDoc;
    jsonDoc["id"] = cellId;
    jsonDoc["temp"] = currentTemp;
    jsonDoc["hum"] = currentHum;
    jsonDoc["ldr"] = currentLight;
    jsonDoc["co2"] = currentCO2;
    jsonDoc["soil"] = currentSoil;
    jsonDoc["water"] = currentWaterLevel;

    jsonDoc["air_pump_state"] = digitalRead(AIR_PUMP);
    jsonDoc["water_pump_state"] = digitalRead(WATER_PUMP);
    jsonDoc["light_state"] = digitalRead(LIGHT);
    jsonDoc["heater_state"] = digitalRead(HEATER);
    
    // Add ISO 8601 timestamp
    jsonDoc["timestamp"] = getISOTimestamp();

    String fullData;
    serializeJson(jsonDoc, fullData);
    
    if (!client.publish("wh/esp/data", fullData.c_str())) {
      Serial.println("[ERROR] Failed to publish sensor data");
    }
  }
}

void storeIDs(String cellID, String warehouseID) {
  Serial.println("[DEBUG] Storing IDs to EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  
  // Clear EEPROM first
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  
  // Store cellId
  int index = 0;
  for (int i = 0; i < cellID.length(); i++) {
    EEPROM.write(index++, cellID[i]);
  }
  EEPROM.write(index++, '\0'); // Null terminator for cellId
  
  // Store warehouseId
  for (int i = 0; i < warehouseID.length(); i++) {
    EEPROM.write(index++, warehouseID[i]);
  }
  EEPROM.write(index++, '\0'); // Null terminator for warehouseId
  
  EEPROM.commit();
  Serial.println("[DEBUG] Successfully stored IDs - CellId: " + cellID + ", WarehouseId: " + warehouseID);
}

void readStoredIDs() {
  Serial.println("[DEBUG] Reading stored IDs from EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  
  // Read cellId
  cellId = "";
  int index = 0;
  while (index < EEPROM_SIZE) {
    char ch = EEPROM.read(index);
    if (ch == '\0' || ch == 255) break;
    cellId += ch;
    index++;
  }
  index++; // Skip null terminator
  
  // Read warehouseId
  warehouseId = "";
  while (index < EEPROM_SIZE) {
    char ch = EEPROM.read(index);
    if (ch == '\0' || ch == 255) break;
    warehouseId += ch;
    index++;
  }
  
  Serial.println("[DEBUG] Read stored IDs - CellId: " + cellId + ", WarehouseId: " + warehouseId);
}

void requestID() {
  Serial.println("[DEBUG] Requesting ID assignment from server (attempt " + String(idRequestAttempts + 1) + "/" + String(MAX_ID_ATTEMPTS) + ")...");
  StaticJsonDocument<100> jsonDoc;
  jsonDoc["mac"] = espMAC;
  char buffer[100];
  serializeJson(jsonDoc, buffer);
  Serial.println("[DEBUG] Sending ID request with MAC: " + espMAC);
  
  // Subscribe to the MAC-specific response topic
  String responseTopicMAC = "esp/" + espMAC + "/id/response";
  client.subscribe(responseTopicMAC.c_str());
  Serial.println("[DEBUG] Subscribed to topic: " + responseTopicMAC);
  
  // Publish ID request
  if (client.publish("esp/id/request", buffer)) {
    Serial.println("[DEBUG] ID request published successfully");
    lastIDRequest = millis();
    idRequestAttempts++;
    waitingForID = true;
  } else {
    Serial.println("[ERROR] Failed to publish ID request");
    // Still increment attempt counter to trigger backoff
    idRequestAttempts++;
    lastIDRequest = millis();
  }
}

void handleIDRequestRetry() {
  // Only retry if we don't have a valid ID and haven't exceeded max attempts
  if (cellId.length() == 0 && idRequestAttempts < MAX_ID_ATTEMPTS) {
    // Calculate exponential backoff delay: 2^attempt * base_delay (capped at reasonable max)
    unsigned long backoffDelay = BASE_RETRY_DELAY * (1UL << min(idRequestAttempts, 8)); // Cap at 2^8 = 256x multiplier
    
    // Check if enough time has passed for the next retry
    if (millis() - lastIDRequest >= backoffDelay) {
      Serial.println("[RETRY] ID request timeout, retrying with " + String(backoffDelay/1000.0) + "s backoff...");
      requestID();
    }
  } else if (cellId.length() == 0 && idRequestAttempts >= MAX_ID_ATTEMPTS) {
    // Max attempts reached, wait longer before resetting
    if (millis() - lastIDRequest >= 300000) { // Wait 5 minutes before resetting
      Serial.println("[RETRY] Max ID request attempts reached, resetting attempt counter...");
      idRequestAttempts = 0;
      waitingForID = false;
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];

  String topicStr = String(topic);
  Serial.println("[DEBUG] Received MQTT message on topic: " + topicStr);
  Serial.println("[DEBUG] Message content: " + message);
  
  // Check if this is an ID response for this specific MAC
  if (topicStr.startsWith("esp/" + espMAC + "/id/response")) {
    Serial.println("[DEBUG] Processing ID assignment response...");
    StaticJsonDocument<200> jsonDoc;
    if (!deserializeJson(jsonDoc, message)) {
      String receivedCellId = jsonDoc["cellId"];
      String receivedWarehouseId = jsonDoc["warehouseId"];
      
      if (receivedCellId.length() > 0 && receivedWarehouseId.length() > 0) {
        cellId = receivedCellId;
        warehouseId = receivedWarehouseId;
        storeIDs(cellId, warehouseId);
        
        // Reset ID request state
        waitingForID = false;
        idRequestAttempts = 0;
        
        // Unsubscribe from ID response and subscribe to command topics
        client.unsubscribe(topicStr.c_str());
        client.subscribe(commandTopic.c_str());
        
        Serial.println("[SUCCESS] ID assignment completed successfully!");
        Serial.println("[DEBUG] Now listening for commands on topic: " + commandTopic);
      } else {
        Serial.println("[ERROR] Received invalid ID response - missing cellId or warehouseId");
        // Don't reset attempt counter, let retry logic handle it
      }
    } else {
      Serial.println("[ERROR] Failed to parse ID response JSON");
      // Don't reset attempt counter, let retry logic handle it
    }
  }
  else if (String(topic) == commandTopic) {
    Serial.println("[DEBUG] Processing actuator command...");
    StaticJsonDocument<300> jsonDoc;
    if (!deserializeJson(jsonDoc, message)) {
      String receivedID = jsonDoc["id"];
      if (receivedID == cellId || receivedID == "all") {
        Serial.println("[DEBUG] Command is for this device (ID: " + cellId + ")");
        int air_pump = jsonDoc["air_pump"];
        int water_pump = jsonDoc["water_pump"];
        int light = jsonDoc["light"];
        int heater = jsonDoc["heater"];

        // Water pump control
        if (water_pump == HIGH && !waterPumpActive) { 
          waterPumpStart = millis(); waterPumpActive = true; 
          Serial.println("[ACTUATOR] Water pump turned ON");
        }
        else if (water_pump == LOW && waterPumpActive) { 
          waterPumpTotalTime += millis() - waterPumpStart; waterPumpActive = false; 
          Serial.println("[ACTUATOR] Water pump turned OFF");
        }

        // Air pump control
        if (air_pump == HIGH && !airPumpActive) { 
          airPumpStart = millis(); airPumpActive = true; 
          Serial.println("[ACTUATOR] Air pump turned ON");
        }
        else if (air_pump == LOW && airPumpActive) { 
          airPumpTotalTime += millis() - airPumpStart; airPumpActive = false; 
          Serial.println("[ACTUATOR] Air pump turned OFF");
        }

        // Light control
        if (light == HIGH && !lightActive) { 
          lightStart = millis(); lightActive = true; 
          Serial.println("[ACTUATOR] Light turned ON");
        }
        else if (light == LOW && lightActive) { 
          lightTotalTime += millis() - lightStart; lightActive = false; 
          Serial.println("[ACTUATOR] Light turned OFF");
        }

        // Heater control
        if (heater == HIGH && !heaterActive) { 
          heaterStart = millis(); heaterActive = true; 
          Serial.println("[ACTUATOR] Heater turned ON");
        }
        else if (heater == LOW && heaterActive) { 
          heaterTotalTime += millis() - heaterStart; heaterActive = false; 
          Serial.println("[ACTUATOR] Heater turned OFF");
        }

        digitalWrite(AIR_PUMP, air_pump);
        digitalWrite(WATER_PUMP, water_pump);
        digitalWrite(LIGHT, light);
        digitalWrite(HEATER, heater);
        
        Serial.println("[SUCCESS] Manual command executed successfully");
      } else {
        Serial.println("[DEBUG] Command ignored - not for this device (received ID: " + receivedID + ", my ID: " + cellId + ")");
      }
    } else {
      Serial.println("[ERROR] Failed to parse command JSON");
    }
  }
  else if (String(topic) == "esp/mode/change") {
    Serial.println("[DEBUG] Processing mode change command...");
    StaticJsonDocument<100> modeDoc;
    if (!deserializeJson(modeDoc, message)) {
      String targetID = modeDoc["id"];
      String newMode = modeDoc["mode"];
      if ((targetID == cellId || targetID == "all") && (newMode == "auto" || newMode == "manual")) {
        String oldMode = operationMode;
        operationMode = newMode;
        Serial.println("[MODE] Operation mode changed from '" + oldMode + "' to '" + newMode + "'");
      } else {
        Serial.println("[DEBUG] Mode change ignored - invalid target ID or mode");
      }
    } else {
      Serial.println("[ERROR] Failed to parse mode change JSON");
    }
  }
}

void publishConsumption() {
  Serial.println("[DEBUG] Publishing hourly consumption data...");
  unsigned long currentTime = millis();

  if (waterPumpActive) { waterPumpTotalTime += currentTime - waterPumpStart; waterPumpStart = currentTime; }
  if (airPumpActive) { airPumpTotalTime += currentTime - airPumpStart; airPumpStart = currentTime; }
  if (lightActive) { lightTotalTime += currentTime - lightStart; lightStart = currentTime; }
  if (heaterActive) { heaterTotalTime += currentTime - heaterStart; heaterStart = currentTime; }

  float waterConsumption = (waterPumpTotalTime / 60000.0) * WATER_FLOW_RATE;
  float airConsumption = (airPumpTotalTime / 60000.0) * AIR_FLOW_RATE;
  float lightConsumption = (lightTotalTime / 60000.0) * LIGHT_POWER;
  float heaterConsumption = (heaterTotalTime / 60000.0) * HEATER_POWER;

  Serial.println("[CONSUMPTION] Water: " + String(waterConsumption) + "L, Air: " + String(airConsumption) + "L, Light: " + String(lightConsumption) + "Wh, Heater: " + String(heaterConsumption) + "Wh");

  StaticJsonDocument<250> jsonDoc;
  jsonDoc["id"] = cellId;
  jsonDoc["water_pump"] = waterConsumption;
  jsonDoc["air_pump"] = airConsumption;
  jsonDoc["light"] = lightConsumption;
  jsonDoc["heater"] = heaterConsumption;

  char buffer[250];
  serializeJson(jsonDoc, buffer);
  
  if (client.publish("wh/esp/consumption", buffer)) {
    Serial.println("[SUCCESS] Consumption data published successfully");
  } else {
    Serial.println("[ERROR] Failed to publish consumption data");
  }

  waterPumpTotalTime = airPumpTotalTime = lightTotalTime = heaterTotalTime = 0;
}

void reconnect() {
  int attempts = 0;
  while (!client.connected() && attempts < 5) {
    Serial.println("[MQTT] Attempting to connect to MQTT broker...");
    
    // Use unique client ID with MAC address
    String clientId = "ESP8266_" + espMAC;
    clientId.replace(":", "");
    
    if (client.connect(clientId.c_str())) {
      Serial.println("[MQTT] Successfully connected to MQTT broker!");
      
      if (cellId.length() == 0) {
        String responseTopicMAC = "esp/" + espMAC + "/id/response";
        client.subscribe(responseTopicMAC.c_str());
        Serial.println("[DEBUG] Subscribed to ID response topic: " + responseTopicMAC);
      }
      client.subscribe("esp/mode/change");
      client.subscribe(commandTopic.c_str());
      Serial.println("[DEBUG] Subscribed to standard topics");
      return;
    } else {
      Serial.println("[ERROR] MQTT connection failed, rc=" + String(client.state()));
      attempts++;
      delay(2000 * attempts); // Exponential backoff
    }
  }
  
  if (attempts >= 5) {
    Serial.println("[ERROR] Max MQTT connection attempts reached, will retry in main loop");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[SYSTEM] ESP8266 IoT Controller Starting...");
  
  EEPROM.begin(EEPROM_SIZE);

  pinMode(AIR_PUMP, OUTPUT);
  pinMode(WATER_PUMP, OUTPUT);
  pinMode(LIGHT, OUTPUT);
  pinMode(HEATER, OUTPUT);
  pinMode(RESET_BUTTON, INPUT_PULLUP);  // Configure reset button with internal pullup
  Serial.println("[SYSTEM] GPIO pins configured for actuators and reset button");

  Serial.println("[WIFI] Connecting to WiFi network: " + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WIFI] WiFi connected successfully!");
  Serial.println("[WIFI] IP address: " + WiFi.localIP().toString());

  // Initialize NTP client
  timeClient.begin();
  Serial.println("[NTP] NTP client initialized");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.println("[MQTT] MQTT client configured for server: " + String(mqtt_server));
  
  reconnect();

  espMAC = WiFi.macAddress();
  Serial.println("[SYSTEM] Device MAC address: " + espMAC);
  
  readStoredIDs();
  
  if (cellId.length() < 1 || warehouseId.length() < 1) {
    Serial.println("[SYSTEM] No valid IDs found in storage, requesting new assignment...");
    cellId = "";
    warehouseId = "";
    // Initial ID request will be handled in the main loop
    idRequestAttempts = 0;
    waitingForID = false;
  } else {
    client.subscribe(commandTopic.c_str());
    Serial.println("[SYSTEM] Using stored IDs - CellId: " + cellId + ", WarehouseId: " + warehouseId);
    Serial.println("[SYSTEM] Ready for operation in " + operationMode + " mode");
  }
}

void loop() {
  // Priority 1: Handle reset button (highest priority for user interaction)
  handleResetButton();
  
  // Priority 2: MQTT processing (critical for commands)
  client.loop();
  
  // Priority 3: Handle ID request retries with exponential backoff
  if (cellId.length() == 0) {
    handleIDRequestRetry();
  }
  
  // Priority 4: Process serial data
  if (Serial.available()) {
    processIncomingData();
  }
  
  // Priority 5: Auto controls (only if we have valid ID)
  if (operationMode == "auto" && cellId.length() > 0) {
    handleAutoControls();
  }
  
  // Priority 6: Data forwarding (NON-BLOCKING, only if we have valid ID)
  static unsigned long lastForward = 0;
  if (millis() - lastForward >= FORWARD_INTERVAL) {
    lastForward = millis();
    if (cellId.length() > 0) {
      forwardSensorData();
    }
  }
  
  // Move connection checks to lowest priority
  static unsigned long lastMQTTCheck = 0;
  if (millis() - lastMQTTCheck >= 30000) {
    lastMQTTCheck = millis();
    if (!client.connected()) {
      reconnect();
    }
  }
  
  static unsigned long lastConsumption = 0;
  if (millis() - lastConsumption >= 10000) {
    lastConsumption = millis();
    if (cellId.length() > 0) {
      publishConsumption();
    }
  }
}

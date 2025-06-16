#include <DHT.h>
#include <ArduinoJson.h>

#define DHTPIN 2
#define DHTTYPE DHT11

#define LDR_PIN A0      
#define CO2_PIN A1      
#define SOIL_PIN A2      
#define WATER_PIN A3     

DHT dht(DHTPIN, DHTTYPE);

void setup() {
    Serial.begin(115200);
    dht.begin();
}

void loop() {
    // Read sensors every 2 seconds
    static unsigned long lastReadTime = 0;
    if (millis() - lastReadTime >= 2000) {
        lastReadTime = millis();
        
        float temp = dht.readTemperature();
        float hum = dht.readHumidity();
        int ldrValue = analogRead(LDR_PIN);
        int co2Value = analogRead(CO2_PIN);
        int soilValue = analogRead(SOIL_PIN);
        int waterValue = analogRead(WATER_PIN);

        if (!isnan(temp) && !isnan(hum)) {
            StaticJsonDocument<200> jsonData;
            jsonData["temp"] = temp;
            jsonData["hum"] = hum;
            jsonData["ldr"] = ldrValue;
            jsonData["co2"] = co2Value;
            jsonData["soil"] = soilValue;
            jsonData["water"] = waterValue;

            String output;
            serializeJson(jsonData, output);
            Serial.println(output);
        }
    }
}

#include <Arduino.h>



#include <DHT.h>



#include <OneWire.h>



#include <DallasTemperature.h>



#include <Wire.h>



#include <LiquidCrystal_I2C.h>







static constexpr char BTS_ID[] = "BTS-001";













static constexpr int PIN_DHT = 15;



static constexpr int PIN_PA_TEMP = 4;







static constexpr int PIN_RSSI = 34;



static constexpr int PIN_TRAFFIC = 35;



static constexpr int PIN_RF_FORWARD = 32;



static constexpr int PIN_RF_REFLECTED = 33;



static constexpr int PIN_LATENCY = 36;



static constexpr int PIN_PACKET_LOSS = 39;







static constexpr int PIN_RADIO_ENABLE = 5;



static constexpr int PIN_BACKHAUL_ENABLE = 27;
static constexpr int PIN_UPSTREAM_REACHABLE = 13;



static constexpr int PIN_FAN_ENABLE = 18;



static constexpr int PIN_INTERFERENCE = 19;



static constexpr int PIN_RADIO_FAULT = 23;







static constexpr int PIN_LED_RADIO = 25;



static constexpr int PIN_LED_BACKHAUL = 26;



static constexpr int PIN_LED_ALARM = 2;



static constexpr int PIN_LED_TX = 14;







DHT dht(PIN_DHT, DHT22);



OneWire oneWire(PIN_PA_TEMP);



DallasTemperature paSensor(&oneWire);



LiquidCrystal_I2C lcd(0x27, 20, 4);







// BTS_NONBLOCKING_IO_V1
static constexpr unsigned long TELEMETRY_INTERVAL_MS = 1000;
static constexpr unsigned long DHT_INTERVAL_MS = 2000;
static constexpr unsigned long PA_CONVERSION_MS = 800;
static constexpr unsigned long LCD_INTERVAL_MS = 250;

unsigned long lastTelemetryMs = 0;
unsigned long lastTxBlinkMs = 0;
unsigned long lastDhtMs = 0;
unsigned long lastPaRequestMs = 0;
unsigned long lastLcdMs = 0;

bool txLedState = false;







float lastCabinetTemp = 28.0f;



float lastHumidity = 45.0f;



float lastPaTemp = 42.0f;







float scaleAnalog(int raw, float low, float high) {



  return low + (static_cast<float>(raw) / 4095.0f) * (high - low);



}







const char* boolJson(bool value) {



  return value ? "true" : "false";



}







void lcdLine(int row, String text) {



  if (text.length() > 20) text = text.substring(0, 20);



  while (text.length() < 20) text += " ";



  lcd.setCursor(0, row);



  lcd.print(text);



}







void setup() {



  Serial.begin(115200);







  pinMode(PIN_RADIO_ENABLE, INPUT);



  pinMode(PIN_BACKHAUL_ENABLE, INPUT);
  pinMode(PIN_UPSTREAM_REACHABLE, INPUT_PULLUP);



  pinMode(PIN_FAN_ENABLE, INPUT);



  pinMode(PIN_INTERFERENCE, INPUT);



  pinMode(PIN_RADIO_FAULT, INPUT);







  pinMode(PIN_LED_RADIO, OUTPUT);



  pinMode(PIN_LED_BACKHAUL, OUTPUT);



  pinMode(PIN_LED_ALARM, OUTPUT);



  pinMode(PIN_LED_TX, OUTPUT);







  dht.begin();
  paSensor.begin();

  // Keep DS18B20 conversion off the critical loop path.
  paSensor.setWaitForConversion(false);
  paSensor.requestTemperatures();
  lastPaRequestMs = millis();







  Wire.begin(21, 22);



  lcd.init();



  lcd.backlight();







  lcdLine(0, "BTS-001 STARTING...");



  lcdLine(1, "Radio hardware plant");



  lcdLine(2, "RFC2217 :4010");



  lcdLine(3, "Waiting telemetry");



  delay(1200);







  Serial.println("BTS_HW_BOOT|BTS-001|READY");



}







void loop() {



  const unsigned long now = millis();







  const bool radioSwitch = digitalRead(PIN_RADIO_ENABLE) == HIGH;



  const bool backhaulAvailable = digitalRead(PIN_BACKHAUL_ENABLE) == HIGH;
  const bool upstreamReachable = backhaulAvailable &&
      (digitalRead(PIN_UPSTREAM_REACHABLE) == HIGH);



  const bool fanOperational = digitalRead(PIN_FAN_ENABLE) == HIGH;



  const bool interference = digitalRead(PIN_INTERFERENCE) == HIGH;



  const bool radioFault = digitalRead(PIN_RADIO_FAULT) == HIGH;



  const bool radioOperational = radioSwitch && !radioFault;







  // Slow environmental sensors are scheduled instead of read every loop.
  if (now - lastDhtMs >= DHT_INTERVAL_MS) {
    lastDhtMs = now;

    float cabinetTemp = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (!isnan(cabinetTemp)) lastCabinetTemp = cabinetTemp;
    if (!isnan(humidity)) lastHumidity = humidity;
  }

  if (now - lastPaRequestMs >= PA_CONVERSION_MS) {
    float paTemp = paSensor.getTempCByIndex(0);

    if (paTemp > -100.0f && paTemp < 150.0f) {
      lastPaTemp = paTemp;
    }

    // Start the next conversion without waiting for it.
    paSensor.requestTemperatures();
    lastPaRequestMs = now;
  }







  const float rssiDbm = scaleAnalog(analogRead(PIN_RSSI), -110.0f, -45.0f);



  const float trafficPct = scaleAnalog(analogRead(PIN_TRAFFIC), 0.0f, 100.0f);



  const float rfForwardW = scaleAnalog(analogRead(PIN_RF_FORWARD), 0.0f, 100.0f);



  const float rfReflectedW = scaleAnalog(analogRead(PIN_RF_REFLECTED), 0.0f, 12.0f);



  const float latencyMs = scaleAnalog(analogRead(PIN_LATENCY), 10.0f, 500.0f);



  const float packetLossPct = scaleAnalog(analogRead(PIN_PACKET_LOSS), 0.0f, 20.0f);







  const float reflectionRatio =



      rfForwardW > 0.5f ? rfReflectedW / rfForwardW : 0.0f;







  const bool thermalAlarm = lastPaTemp >= 75.0f || !fanOperational;



  const bool rfAlarm =



      radioFault ||



      interference ||



      rssiDbm < -95.0f ||



      reflectionRatio > 0.25f;







  const bool backhaulAlarm =



      !backhaulAvailable ||



      latencyMs > 180.0f ||



      packetLossPct > 5.0f;







  const bool alarm = thermalAlarm || rfAlarm || backhaulAlarm;







  digitalWrite(PIN_LED_RADIO, radioOperational ? HIGH : LOW);



  digitalWrite(PIN_LED_BACKHAUL, backhaulAvailable ? HIGH : LOW);



  digitalWrite(PIN_LED_ALARM, alarm ? HIGH : LOW);







  if (radioOperational && trafficPct > 2.0f) {



    if (now - lastTxBlinkMs >= 180) {



      lastTxBlinkMs = now;



      txLedState = !txLedState;



      digitalWrite(PIN_LED_TX, txLedState ? HIGH : LOW);



    }



  } else {



    txLedState = false;



    digitalWrite(PIN_LED_TX, LOW);



  }
  if (now - lastLcdMs >= LCD_INTERVAL_MS) {
    lastLcdMs = now;

    lcdLine(
        0,
        String("BTS-001 R:") + (radioOperational ? "ON " : "OFF") +
        " B:" + (backhaulAvailable ? "ON" : "OFF")
    );
    lcdLine(
        1,
        String("PA ") + String(lastPaTemp, 1) +
        "C RSSI " + String(rssiDbm, 0)
    );
    lcdLine(
        2,
        String("TRF ") + String(trafficPct, 0) +
        "% LOSS " + String(packetLossPct, 1)
    );
    lcdLine(
        3,
        String("LAT ") + String(latencyMs, 0) +
        "ms ALM " + (alarm ? "YES" : "NO")
    );
  }







  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {



    lastTelemetryMs = now;







    Serial.print("BTS_HW|{");



    Serial.print("\"schema\":\"bts.hw.v1\",");



    Serial.print("\"bts_id\":\"");



    Serial.print(BTS_ID);



    Serial.print("\",");



    Serial.print("\"timestamp_ms\":");



    Serial.print(now);



    Serial.print(",");



    Serial.print("\"cabinet_temp_c\":");



    Serial.print(lastCabinetTemp, 2);



    Serial.print(",");



    Serial.print("\"humidity_pct\":");



    Serial.print(lastHumidity, 2);



    Serial.print(",");



    Serial.print("\"pa_temp_c\":");



    Serial.print(lastPaTemp, 2);



    Serial.print(",");



    Serial.print("\"rssi_dbm\":");



    Serial.print(rssiDbm, 2);



    Serial.print(",");



    Serial.print("\"traffic_load_pct\":");



    Serial.print(trafficPct, 2);



    Serial.print(",");



    Serial.print("\"rf_forward_w\":");



    Serial.print(rfForwardW, 2);



    Serial.print(",");



    Serial.print("\"rf_reflected_w\":");



    Serial.print(rfReflectedW, 2);



    Serial.print(",");



    Serial.print("\"reflection_ratio\":");



    Serial.print(reflectionRatio, 4);



    Serial.print(",");



    Serial.print("\"latency_ms\":");



    Serial.print(latencyMs, 2);



    Serial.print(",");



    Serial.print("\"packet_loss_pct\":");



    Serial.print(packetLossPct, 2);



    Serial.print(",");



    Serial.print("\"radio_operational\":");



    Serial.print(boolJson(radioOperational));



    Serial.print(",");



    Serial.print("\"backhaul_available\":");



    Serial.print(boolJson(upstreamReachable));



    Serial.print(",");



    Serial.print("\"link_up\":");
Serial.print(boolJson(backhaulAvailable));
Serial.print(",");
Serial.print("\"upstream_reachable\":");
Serial.print(boolJson(upstreamReachable));
Serial.print(",");
Serial.print("\"fan_operational\":");



    Serial.print(boolJson(fanOperational));



    Serial.print(",");



    Serial.print("\"interference\":");



    Serial.print(boolJson(interference));



    Serial.print(",");



    Serial.print("\"radio_fault\":");



    Serial.print(boolJson(radioFault));



    Serial.print(",");



    Serial.print("\"alarm\":");



    Serial.print(boolJson(alarm));



    Serial.println("}");



  }







  delay(5);



}




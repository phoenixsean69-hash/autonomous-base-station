#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

// ====================================================
// PIN DEFINITIONS
// ====================================================

#define DHT_PIN 15
#define DHT_TYPE DHT22

#define DS18B20_PIN 4

#define DC_VOLTAGE_PIN 34
#define DC_CURRENT_PIN 35
#define BATTERY_VOLTAGE_PIN 32

#define RF_FORWARD_PIN 33
#define RF_REFLECTED_PIN 36

#define BACKHAUL_LATENCY_PIN 25
#define BACKHAUL_LOSS_PIN 26
#define BACKHAUL_RSSI_PIN 27

#define BACKHAUL_LINK_PIN 14
#define UPSTREAM_REACHABLE_PIN 13

const float GRAVITY = 9.80665;

// ====================================================
// SENSOR OBJECTS
// ====================================================

DHT dht(
    DHT_PIN,
    DHT_TYPE
);

Adafruit_MPU6050 mpu;

OneWire oneWire(
    DS18B20_PIN
);

DallasTemperature paTemperatureSensor(
    &oneWire
);

// ====================================================
// LCD DISPLAYS
// ====================================================

// DC power
LiquidCrystal_I2C lcdDcVoltage(
    0x20,
    16,
    2
);

LiquidCrystal_I2C lcdDcCurrent(
    0x21,
    16,
    2
);

LiquidCrystal_I2C lcdBattery(
    0x22,
    16,
    2
);

// RF
LiquidCrystal_I2C lcdRfForward(
    0x23,
    16,
    2
);

LiquidCrystal_I2C lcdRfReflected(
    0x24,
    16,
    2
);

// Backhaul overall status
LiquidCrystal_I2C lcdBackhaul(
    0x25,
    16,
    2
);

// Independent backhaul metrics
LiquidCrystal_I2C lcdLatency(
    0x26,
    16,
    2
);

LiquidCrystal_I2C lcdPacketLoss(
    0x27,
    16,
    2
);

LiquidCrystal_I2C lcdRssi(
    0x3F,
    16,
    2
);

// ====================================================
// LCD HELPERS
// ====================================================

void printLCDLine(
    LiquidCrystal_I2C &lcd,
    int row,
    String text)
{
  if (text.length() > 16)
  {
    text =
        text.substring(
            0,
            16
        );
  }

  while (text.length() < 16)
  {
    text += " ";
  }

  lcd.setCursor(
      0,
      row
  );

  lcd.print(
      text
  );
}

void updateLCD(
    LiquidCrystal_I2C &lcd,
    String label,
    String value)
{
  printLCDLine(
      lcd,
      0,
      label
  );

  printLCDLine(
      lcd,
      1,
      value
  );
}

// ====================================================
// BATTERY
// ====================================================

float calculateBatterySOC(
    float voltage)
{
  if (voltage <= 10.5)
  {
    return 0.0;
  }

  if (voltage >= 12.7)
  {
    return 100.0;
  }

  return
      ((voltage - 10.5) /
       (12.7 - 10.5)) *
      100.0;
}

// ====================================================
// RF FUNCTIONS
// ====================================================

bool isRFMeasurementValid(
    float forwardPower,
    float reflectedPower)
{
  if (forwardPower <= 0.01)
  {
    return false;
  }

  if (
      reflectedPower >=
      forwardPower)
  {
    return false;
  }

  return true;
}

float calculateVSWR(
    float forwardPower,
    float reflectedPower)
{
  float reflectionCoefficient =
      sqrt(
          reflectedPower /
          forwardPower
      );

  return
      (1.0 +
       reflectionCoefficient) /
      (1.0 -
       reflectionCoefficient);
}

float calculateReturnLoss(
    float forwardPower,
    float reflectedPower)
{
  if (
      reflectedPower <=
      0.001)
  {
    return 60.0;
  }

  return
      10.0 *
      log10(
          forwardPower /
          reflectedPower
      );
}

String determineRFHealth(
    bool valid,
    float vswr)
{
  if (!valid)
  {
    return "SEVERE FAULT";
  }

  if (vswr < 1.5)
  {
    return "EXCELLENT";
  }

  if (vswr < 2.0)
  {
    return "NORMAL";
  }

  if (vswr < 3.0)
  {
    return "DEGRADED";
  }

  return "FAULT";
}

// ====================================================
// ELECTRICAL HEALTH
// ====================================================

String determineElectricalHealth(
    float voltage,
    float current,
    float power,
    float rfForwardPower)
{
  // Severe voltage fault
  if (
      voltage < 40.0 ||
      voltage > 58.0)
  {
    return "FAULT";
  }

  // Severe overcurrent
  if (current > 25.0)
  {
    return "FAULT";
  }

  // Severe power overload
  if (power > 1200.0)
  {
    return "FAULT";
  }

  // Transmitter appears active but DC current is
  // extremely low.
  if (
      rfForwardPower > 20.0 &&
      current < 1.0)
  {
    return "FAULT";
  }

  // Degraded voltage
  if (
      voltage < 44.0 ||
      voltage > 55.0)
  {
    return "DEGRADED";
  }

  // High current
  if (current > 20.0)
  {
    return "DEGRADED";
  }

  // High power
  if (power > 900.0)
  {
    return "DEGRADED";
  }

  return "NORMAL";
}

// ====================================================
// BACKHAUL FUNCTIONS
// ====================================================

float calculateLinkQuality(
    float rssi)
{
  if (rssi >= -45.0)
  {
    return 100.0;
  }

  if (rssi <= -120.0)
  {
    return 0.0;
  }

  return
      ((rssi + 120.0) /
       75.0) *
      100.0;
}

String determineBackhaulCondition(
    bool linkUp,
    bool upstreamReachable,
    float latency,
    float packetLoss,
    float rssi)
{
  // ==================================================
  // OUTAGE
  // ==================================================

  if (
      !linkUp ||
      !upstreamReachable)
  {
    return "OUTAGE";
  }

  // ==================================================
  // CRITICAL
  // ==================================================

  if (
      latency >= 600.0 ||
      packetLoss >= 60.0 ||
      rssi <= -95.0)
  {
    return "CRITICAL";
  }

  // ==================================================
  // DEGRADED
  // ==================================================

  if (
      latency >= 200.0 ||
      packetLoss >= 10.0 ||
      rssi <= -80.0)
  {
    return "DEGRADED";
  }

  return "NORMAL";
}

// ====================================================
// LOCAL SITE STATUS
// ====================================================

String determineLocalSiteStatus(
    String rfHealth,
    String electricalHealth,
    float paTemperature,
    float batterySOC,
    float dynamicVibration)
{
  // ==================================================
  // FAULT
  // ==================================================

  if (
      rfHealth == "FAULT" ||
      rfHealth == "SEVERE FAULT")
  {
    return "FAULT";
  }

  if (
      electricalHealth ==
      "FAULT")
  {
    return "FAULT";
  }

  if (
      paTemperature >=
      80.0)
  {
    return "FAULT";
  }

  if (
      dynamicVibration >=
      3.0)
  {
    return "FAULT";
  }

  // ==================================================
  // DEGRADED
  // ==================================================

  if (
      rfHealth ==
      "DEGRADED")
  {
    return "DEGRADED";
  }

  if (
      electricalHealth ==
      "DEGRADED")
  {
    return "DEGRADED";
  }

  if (
      paTemperature >=
      65.0)
  {
    return "DEGRADED";
  }

  if (
      batterySOC <
      30.0)
  {
    return "DEGRADED";
  }

  if (
      dynamicVibration >=
      1.0)
  {
    return "DEGRADED";
  }

  return "NORMAL";
}

// ====================================================
// FAULT DIFFERENTIATION
// ====================================================

String determineFaultCandidate(
    String localStatus,
    String backhaulStatus)
{
  bool localProblem =
      (
          localStatus ==
              "FAULT" ||
          localStatus ==
              "DEGRADED"
      );

  bool upstreamProblem =
      (
          backhaulStatus ==
              "DEGRADED" ||
          backhaulStatus ==
              "CRITICAL" ||
          backhaulStatus ==
              "OUTAGE"
      );

  if (
      localProblem &&
      upstreamProblem)
  {
    return "MIXED_FAULT";
  }

  if (localProblem)
  {
    return "LOCAL_FAULT";
  }

  if (upstreamProblem)
  {
    return "UPSTREAM_FAULT";
  }

  return "NORMAL";
}

// ====================================================
// LCD INITIALISATION
// ====================================================

void initialiseLCDs()
{
  lcdDcVoltage.init();
  lcdDcCurrent.init();
  lcdBattery.init();

  lcdRfForward.init();
  lcdRfReflected.init();

  lcdBackhaul.init();

  lcdLatency.init();
  lcdPacketLoss.init();
  lcdRssi.init();

  lcdDcVoltage.backlight();
  lcdDcCurrent.backlight();
  lcdBattery.backlight();

  lcdRfForward.backlight();
  lcdRfReflected.backlight();

  lcdBackhaul.backlight();

  lcdLatency.backlight();
  lcdPacketLoss.backlight();
  lcdRssi.backlight();

  updateLCD(
      lcdDcVoltage,
      "DC BUS VOLTAGE",
      "Starting..."
  );

  updateLCD(
      lcdDcCurrent,
      "DC BUS CURRENT",
      "Starting..."
  );

  updateLCD(
      lcdBattery,
      "BATTERY VOLTAGE",
      "Starting..."
  );

  updateLCD(
      lcdRfForward,
      "FORWARD RF",
      "Starting..."
  );

  updateLCD(
      lcdRfReflected,
      "REFLECTED RF",
      "Starting..."
  );

  updateLCD(
      lcdBackhaul,
      "BACKHAUL STATUS",
      "Starting..."
  );

  updateLCD(
      lcdLatency,
      "LATENCY",
      "Starting..."
  );

  updateLCD(
      lcdPacketLoss,
      "PACKET LOSS",
      "Starting..."
  );

  updateLCD(
      lcdRssi,
      "RSSI",
      "Starting..."
  );
}

// ====================================================
// SETUP
// ====================================================

void setup()
{
  Serial.begin(
      115200
  );

  delay(1000);

  Serial.println();

  Serial.println(
      "======================================"
  );

  Serial.println(
      " AUTONOMOUS BASE STATION"
  );

  Serial.println(
      " ESP32 SENSOR NODE"
  );

  Serial.println(
      "======================================"
  );

  // ==================================================
  // ENVIRONMENT
  // ==================================================

  dht.begin();

  // ==================================================
  // I2C
  // ==================================================

  Wire.begin(
      21,
      22
  );

  // ==================================================
  // MPU6050
  // ==================================================

  if (!mpu.begin())
  {
    Serial.println(
        "ERROR: MPU6050 not detected!"
    );

    while (true)
    {
      delay(1000);
    }
  }

  Serial.println(
      "MPU6050 detected."
  );

  // ==================================================
  // DS18B20
  // ==================================================

  paTemperatureSensor.begin();

  Serial.print(
      "DS18B20 devices found: "
  );

  Serial.println(
      paTemperatureSensor
          .getDeviceCount()
  );

  // ==================================================
  // LCDS
  // ==================================================

  initialiseLCDs();

  Serial.println(
      "LCD diagnostic panel ready."
  );

  // ==================================================
  // ANALOG INPUTS
  // ==================================================

  pinMode(
      DC_VOLTAGE_PIN,
      INPUT
  );

  pinMode(
      DC_CURRENT_PIN,
      INPUT
  );

  pinMode(
      BATTERY_VOLTAGE_PIN,
      INPUT
  );

  pinMode(
      RF_FORWARD_PIN,
      INPUT
  );

  pinMode(
      RF_REFLECTED_PIN,
      INPUT
  );

  pinMode(
      BACKHAUL_LATENCY_PIN,
      INPUT
  );

  pinMode(
      BACKHAUL_LOSS_PIN,
      INPUT
  );

  pinMode(
      BACKHAUL_RSSI_PIN,
      INPUT
  );

  // ==================================================
  // DIGITAL BACKHAUL INPUTS
  // ==================================================

  pinMode(
      BACKHAUL_LINK_PIN,
      INPUT
  );

  pinMode(
      UPSTREAM_REACHABLE_PIN,
      INPUT
  );

  Serial.println(
      "Independent backhaul inputs ready."
  );

  Serial.println(
      "Sensor node ready."
  );
}

// ====================================================
// LOOP
// ====================================================

void loop()
{
  // ==================================================
  // ENVIRONMENT
  // ==================================================

  float shelterTemperature =
      dht.readTemperature();

  float humidity =
      dht.readHumidity();

  paTemperatureSensor
      .requestTemperatures();

  float paTemperature =
      paTemperatureSensor
          .getTempCByIndex(0);

  // ==================================================
  // VIBRATION
  // ==================================================

  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t mpuTemp;

  mpu.getEvent(
      &accel,
      &gyro,
      &mpuTemp
  );

  float totalAcceleration =
      sqrt(
          accel.acceleration.x *
              accel.acceleration.x +

          accel.acceleration.y *
              accel.acceleration.y +

          accel.acceleration.z *
              accel.acceleration.z
      );

  float dynamicVibration =
      fabs(
          totalAcceleration -
          GRAVITY
      );

  // ==================================================
  // DC POWER
  // ==================================================

  float dcBusVoltage =
      (
          analogRead(
              DC_VOLTAGE_PIN
          ) /
          4095.0
      ) *
      60.0;

  float dcBusCurrent =
      (
          analogRead(
              DC_CURRENT_PIN
          ) /
          4095.0
      ) *
      30.0;

  float dcPower =
      dcBusVoltage *
      dcBusCurrent;

  // ==================================================
  // BATTERY
  // ==================================================

  float batteryVoltage =
      (
          analogRead(
              BATTERY_VOLTAGE_PIN
          ) /
          4095.0
      ) *
      15.0;

  float batterySOC =
      calculateBatterySOC(
          batteryVoltage
      );

  // ==================================================
  // RF SYSTEM
  // ==================================================

  float rfForwardPower =
      (
          analogRead(
              RF_FORWARD_PIN
          ) /
          4095.0
      ) *
      100.0;

  float rfReflectedPower =
      (
          analogRead(
              RF_REFLECTED_PIN
          ) /
          4095.0
      ) *
      100.0;

  bool rfValid =
      isRFMeasurementValid(
          rfForwardPower,
          rfReflectedPower
      );

  float vswr =
      0.0;

  float returnLoss =
      0.0;

  if (rfValid)
  {
    vswr =
        calculateVSWR(
            rfForwardPower,
            rfReflectedPower
        );

    returnLoss =
        calculateReturnLoss(
            rfForwardPower,
            rfReflectedPower
        );
  }

  String rfHealth =
      determineRFHealth(
          rfValid,
          vswr
      );

  // ==================================================
  // ELECTRICAL HEALTH
  // ==================================================

  String electricalHealth =
      determineElectricalHealth(
          dcBusVoltage,
          dcBusCurrent,
          dcPower,
          rfForwardPower
      );

  // ==================================================
  // INDEPENDENT BACKHAUL INPUTS
  // ==================================================

  int latencyRaw =
      analogRead(
          BACKHAUL_LATENCY_PIN
      );

  int packetLossRaw =
      analogRead(
          BACKHAUL_LOSS_PIN
      );

  int rssiRaw =
      analogRead(
          BACKHAUL_RSSI_PIN
      );

  // 10 ms to 1000 ms
  float latency =
      10.0 +
      (
          latencyRaw /
          4095.0
      ) *
      990.0;

  // 0% to 100%
  float packetLoss =
      (
          packetLossRaw /
          4095.0
      ) *
      100.0;

  // -45 dBm to -120 dBm
  float rssi =
      -45.0 -
      (
          rssiRaw /
          4095.0
      ) *
      75.0;

  // ==================================================
  // DIGITAL BACKHAUL STATES
  // ==================================================

  bool linkUp =
      digitalRead(
          BACKHAUL_LINK_PIN
      ) ==
      HIGH;

  bool upstreamReachable =
      digitalRead(
          UPSTREAM_REACHABLE_PIN
      ) ==
      HIGH;

  // ==================================================
  // DERIVED BACKHAUL VALUES
  // ==================================================

  float linkQuality =
      calculateLinkQuality(
          rssi
      );

  String backhaulCondition =
      determineBackhaulCondition(
          linkUp,
          upstreamReachable,
          latency,
          packetLoss,
          rssi
      );

  // ==================================================
  // FINAL FAULT STATUS
  // ==================================================

  String localSiteStatus =
      determineLocalSiteStatus(
          rfHealth,
          electricalHealth,
          paTemperature,
          batterySOC,
          dynamicVibration
      );

  String faultCandidate =
      determineFaultCandidate(
          localSiteStatus,
          backhaulCondition
      );

  // ==================================================
  // DC POWER LCDS
  // ==================================================

  updateLCD(
      lcdDcVoltage,
      "DC BUS VOLTAGE",
      String(
          dcBusVoltage,
          2
      ) +
          " V"
  );

  updateLCD(
      lcdDcCurrent,
      "DC BUS CURRENT",
      String(
          dcBusCurrent,
          2
      ) +
          " A"
  );

  updateLCD(
      lcdBattery,
      "BATTERY VOLTAGE",
      String(
          batteryVoltage,
          2
      ) +
          " V " +
          String(
              batterySOC,
              0
          ) +
          "%"
  );

  // ==================================================
  // RF LCDS
  // ==================================================

  updateLCD(
      lcdRfForward,
      "FORWARD RF",
      String(
          rfForwardPower,
          2
      ) +
          " W"
  );

  updateLCD(
      lcdRfReflected,
      "REFLECTED RF",
      String(
          rfReflectedPower,
          2
      ) +
          " W"
  );

  // ==================================================
  // BACKHAUL STATUS LCD
  // ==================================================

  updateLCD(
      lcdBackhaul,
      "BACKHAUL STATUS",
      backhaulCondition
  );

  // ==================================================
  // INDEPENDENT BACKHAUL METRIC LCDS
  // ==================================================

  updateLCD(
      lcdLatency,
      "LATENCY",
      String(
          latency,
          1
      ) +
          " ms"
  );

  updateLCD(
      lcdPacketLoss,
      "PACKET LOSS",
      String(
          packetLoss,
          1
      ) +
          " %"
  );

  updateLCD(
      lcdRssi,
      "RSSI",
      String(
          rssi,
          1
      ) +
          " dBm"
  );

  // ==================================================
  // SERIAL TELEMETRY
  // ==================================================

  Serial.println();

  Serial.println(
      "================================================"
  );

  Serial.println(
      "       AUTONOMOUS BASE STATION TELEMETRY"
  );

  Serial.println(
      "================================================"
  );

  // ==================================================
  // ENVIRONMENT
  // ==================================================

  Serial.println();

  Serial.println(
      "[ ENVIRONMENT ]"
  );

  Serial.print(
      "Shelter Temperature : "
  );

  Serial.print(
      shelterTemperature,
      2
  );

  Serial.println(
      " C"
  );

  Serial.print(
      "Humidity            : "
  );

  Serial.print(
      humidity,
      2
  );

  Serial.println(
      " %"
  );

  Serial.print(
      "PA Temperature      : "
  );

  Serial.print(
      paTemperature,
      2
  );

  Serial.println(
      " C"
  );

  // ==================================================
  // VIBRATION
  // ==================================================

  Serial.println();

  Serial.println(
      "[ VIBRATION ]"
  );

  Serial.print(
      "Acceleration X      : "
  );

  Serial.print(
      accel.acceleration.x,
      2
  );

  Serial.println(
      " m/s^2"
  );

  Serial.print(
      "Acceleration Y      : "
  );

  Serial.print(
      accel.acceleration.y,
      2
  );

  Serial.println(
      " m/s^2"
  );

  Serial.print(
      "Acceleration Z      : "
  );

  Serial.print(
      accel.acceleration.z,
      2
  );

  Serial.println(
      " m/s^2"
  );

  Serial.print(
      "Total Accel         : "
  );

  Serial.print(
      totalAcceleration,
      2
  );

  Serial.println(
      " m/s^2"
  );

  Serial.print(
      "Dynamic Vibration   : "
  );

  Serial.print(
      dynamicVibration,
      3
  );

  Serial.println(
      " m/s^2"
  );

  // ==================================================
  // DC POWER
  // ==================================================

  Serial.println();

  Serial.println(
      "[ DC POWER SYSTEM ]"
  );

  Serial.print(
      "DC Bus Voltage      : "
  );

  Serial.print(
      dcBusVoltage,
      2
  );

  Serial.println(
      " V"
  );

  Serial.print(
      "DC Bus Current      : "
  );

  Serial.print(
      dcBusCurrent,
      2
  );

  Serial.println(
      " A"
  );

  Serial.print(
      "DC Power            : "
  );

  Serial.print(
      dcPower,
      2
  );

  Serial.println(
      " W"
  );

  Serial.print(
      "Electrical Health   : "
  );

  Serial.println(
      electricalHealth
  );

  // ==================================================
  // BATTERY
  // ==================================================

  Serial.println();

  Serial.println(
      "[ BATTERY BACKUP ]"
  );

  Serial.print(
      "Battery Voltage     : "
  );

  Serial.print(
      batteryVoltage,
      2
  );

  Serial.println(
      " V"
  );

  Serial.print(
      "Battery SoC         : "
  );

  Serial.print(
      batterySOC,
      1
  );

  Serial.println(
      " %"
  );

  // ==================================================
  // RF SYSTEM
  // ==================================================

  Serial.println();

  Serial.println(
      "[ RF SYSTEM ]"
  );

  Serial.print(
      "Forward Power       : "
  );

  Serial.print(
      rfForwardPower,
      2
  );

  Serial.println(
      " W"
  );

  Serial.print(
      "Reflected Power     : "
  );

  Serial.print(
      rfReflectedPower,
      2
  );

  Serial.println(
      " W"
  );

  if (rfValid)
  {
    Serial.print(
        "VSWR                : "
    );

    Serial.println(
        vswr,
        2
    );

    Serial.print(
        "Return Loss         : "
    );

    Serial.print(
        returnLoss,
        2
    );

    Serial.println(
        " dB"
    );

    Serial.println(
        "RF Measurement      : VALID"
    );
  }
  else
  {
    Serial.println(
        "VSWR                : INVALID"
    );

    Serial.println(
        "Return Loss         : INVALID"
    );

    Serial.println(
        "RF Measurement      : INVALID"
    );

    if (
        rfForwardPower <=
        0.01)
    {
      Serial.println(
          "RF Reason           : NO FORWARD POWER"
      );
    }
    else if (
        rfReflectedPower >=
        rfForwardPower)
    {
      Serial.println(
          "RF Reason           : REFLECTED POWER >= FORWARD POWER"
      );
    }
  }

  Serial.print(
      "RF Health           : "
  );

  Serial.println(
      rfHealth
  );

  // ==================================================
  // BACKHAUL
  // ==================================================

  Serial.println();

  Serial.println(
      "[ BACKHAUL / UPSTREAM NETWORK ]"
  );

  Serial.print(
      "Physical Link       : "
  );

  Serial.println(
      linkUp
          ? "UP"
          : "DOWN"
  );

  Serial.print(
      "Upstream Reachable  : "
  );

  Serial.println(
      upstreamReachable
          ? "YES"
          : "NO"
  );

  Serial.print(
      "Latency             : "
  );

  Serial.print(
      latency,
      1
  );

  Serial.println(
      " ms"
  );

  Serial.print(
      "Packet Loss         : "
  );

  Serial.print(
      packetLoss,
      1
  );

  Serial.println(
      " %"
  );

  Serial.print(
      "RSSI                : "
  );

  Serial.print(
      rssi,
      1
  );

  Serial.println(
      " dBm"
  );

  Serial.print(
      "Link Quality        : "
  );

  Serial.print(
      linkQuality,
      1
  );

  Serial.println(
      " %"
  );

  Serial.print(
      "Backhaul Condition  : "
  );

  Serial.println(
      backhaulCondition
  );

  // ==================================================
  // FAULT DIFFERENTIATION
  // ==================================================

  Serial.println();

  Serial.println(
      "[ FAULT DIFFERENTIATION ]"
  );

  Serial.print(
      "RF Health           : "
  );

  Serial.println(
      rfHealth
  );

  Serial.print(
      "Electrical Health   : "
  );

  Serial.println(
      electricalHealth
  );

  Serial.print(
      "Local Site Status   : "
  );

  Serial.println(
      localSiteStatus
  );

  Serial.print(
      "Backhaul Status     : "
  );

  Serial.println(
      backhaulCondition
  );

  Serial.print(
      "Fault Candidate     : "
  );

  Serial.println(
      faultCandidate
  );

  Serial.println(
      "Label Source        : RULE-BASED SIMULATION GROUND TRUTH"
  );

  // ==================================================
  // SYSTEM
  // ==================================================

  Serial.println();

  Serial.println(
      "[ SYSTEM ]"
  );

  Serial.println(
      "Stage               : DATA ACQUISITION"
  );

  Serial.println(
      "AI Classifier       : NOT YET ACTIVE"
  );

  Serial.println();

  Serial.println(
      "Next telemetry update in 2 seconds..."
  );

  delay(
      2000
  );
}
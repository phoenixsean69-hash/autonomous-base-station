#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <arduinoFFT.h>

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

#define GRID_AVAILABLE_PIN 16
#define GENERATOR_RUNNING_PIN 17

// Digital-twin operating-condition inputs
#define TRAFFIC_LOAD_PIN 39
#define FAN_OPERATIONAL_PIN 18
#define RECTIFIER_NORMAL_PIN 19
#define RADIO_OPERATIONAL_PIN 23

// ====================================================
// TIMING
// ====================================================

const unsigned long FAST_INPUT_INTERVAL_MS = 50;
const unsigned long LCD_REFRESH_INTERVAL_MS = 100;
const unsigned long MPU_INTERVAL_MS = 20;
const unsigned long DHT_INTERVAL_MS = 2000;

const unsigned long DS18B20_REQUEST_INTERVAL_MS = 2000;
const unsigned long DS18B20_CONVERSION_MS = 750;

const unsigned long TELEMETRY_INTERVAL_MS = 2000;

const float GRAVITY = 9.80665;

// ====================================================
// VIBRATION SIGNAL PROCESSING
// ====================================================

// 50 Hz sample rate:
// Nyquist frequency = 25 Hz.
//
// 128 samples give a time window of:
// 128 / 50 = 2.56 seconds.
//
// Frequency resolution:
// 50 / 128 = 0.390625 Hz.
const uint16_t FFT_SAMPLES = 128;
const double VIBRATION_SAMPLE_RATE_HZ = 50.0;

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

LiquidCrystal_I2C lcdBackhaul(
    0x25,
    16,
    2
);

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

LiquidCrystal_I2C lcdEnergySource(
    0x3E,
    16,
    2
);

LiquidCrystal_I2C lcdTrafficLoad(
    0x3D,
    16,
    2
);

LiquidCrystal_I2C lcdOperatingMode(
    0x3C,
    16,
    2
);

// ====================================================
// GLOBAL MEASUREMENTS
// ====================================================

// Environment
float shelterTemperature = 0.0;
float humidity = 0.0;
float paTemperature = 0.0;

// Vibration
float accelX = 0.0;
float accelY = 0.0;
float accelZ = 0.0;

float totalAcceleration = 0.0;
float dynamicVibration = 0.0;

// Signed acceleration residual after removing gravity.
// Unlike dynamicVibration, this keeps the positive/negative
// waveform needed for frequency analysis.
float vibrationSignal = 0.0;

// FFT sample buffers.
double vibrationReal[FFT_SAMPLES];
double vibrationImag[FFT_SAMPLES];

uint16_t vibrationSampleIndex = 0;
bool vibrationSpectrumReady = false;

// Time-domain features.
float vibrationMean = 0.0;
float vibrationRMS = 0.0;
float vibrationStdDev = 0.0;
float vibrationPeakToPeak = 0.0;

// Frequency-domain features.
float dominantVibrationFrequencyHz = 0.0;
float vibrationSpectralEnergy = 0.0;
float vibrationSpectralCentroidHz = 0.0;

ArduinoFFT<double> vibrationFFT =
    ArduinoFFT<double>(
        vibrationReal,
        vibrationImag,
        FFT_SAMPLES,
        VIBRATION_SAMPLE_RATE_HZ
    );

// DC power
float dcBusVoltage = 0.0;
float dcBusCurrent = 0.0;
float dcPower = 0.0;

// Battery
float batteryVoltage = 0.0;
float batterySOC = 0.0;

// RF
float rfForwardPower = 0.0;
float rfReflectedPower = 0.0;

bool rfValid = false;

float vswr = 0.0;
float returnLoss = 0.0;

String rfHealth = "UNKNOWN";

// Backhaul
float latency = 0.0;
float packetLoss = 0.0;
float rssi = 0.0;
float linkQuality = 0.0;

bool linkUp = false;
bool upstreamReachable = false;

String backhaulCondition = "UNKNOWN";

// Electrical
String electricalHealth = "UNKNOWN";

// Fault classification
String localSiteStatus = "UNKNOWN";
String faultCandidate = "UNKNOWN";

// Energy
bool gridAvailable = false;
bool generatorRunning = false;

String activePowerSource = "UNKNOWN";
String energyAction = "UNKNOWN";

// Digital-twin operating conditions
float trafficLoad = 0.0;

bool fanOperational = true;
bool rectifierNormal = true;
bool radioOperational = true;

// Baseline energy-management state
String operatingMode = "UNKNOWN";
String operatingModeReason = "STARTING";

float baselineSitePowerKW = 0.0;
float managedSitePowerKW = 0.0;
float estimatedEnergySavingPct = 0.0;

// ====================================================
// TIMERS
// ====================================================

unsigned long lastFastInputTime = 0;
unsigned long lastLCDRefreshTime = 0;
unsigned long lastMPUTime = 0;
unsigned long lastDHTTime = 0;
unsigned long lastTelemetryTime = 0;

unsigned long lastDS18B20RequestTime = 0;

bool ds18b20ConversionPending = false;

// ====================================================
// LCD LAST-DISPLAYED VALUES
// ====================================================

String lastDcVoltageLCD = "";
String lastDcCurrentLCD = "";
String lastBatteryLCD = "";

String lastRfForwardLCD = "";
String lastRfReflectedLCD = "";

String lastBackhaulLCD = "";
String lastLatencyLCD = "";
String lastPacketLossLCD = "";
String lastRssiLCD = "";

String lastEnergySourceLCD = "";
String lastTrafficLoadLCD = "";
String lastOperatingModeLCD = "";

// ====================================================
// LCD HELPERS
// ====================================================

String padLCDText(
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

  return text;
}

void writeLCDLine(
    LiquidCrystal_I2C &lcd,
    uint8_t row,
    const String &text)
{
  lcd.setCursor(
      0,
      row
  );

  lcd.print(
      padLCDText(
          text
      )
  );
}

void setLCDLabel(
    LiquidCrystal_I2C &lcd,
    const String &label)
{
  writeLCDLine(
      lcd,
      0,
      label
  );
}

void updateLCDValueIfChanged(
    LiquidCrystal_I2C &lcd,
    const String &newValue,
    String &oldValue,
    bool force = false)
{
  if (
      force ||
      newValue != oldValue)
  {
    writeLCDLine(
        lcd,
        1,
        newValue
    );

    oldValue =
        newValue;
  }
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
    float currentVswr)
{
  if (!valid)
  {
    return "SEVERE FAULT";
  }

  if (currentVswr < 1.5)
  {
    return "EXCELLENT";
  }

  if (currentVswr < 2.0)
  {
    return "NORMAL";
  }

  if (currentVswr < 3.0)
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
    float forwardPower)
{
  if (
      voltage < 40.0 ||
      voltage > 58.0)
  {
    return "FAULT";
  }

  if (current > 25.0)
  {
    return "FAULT";
  }

  if (power > 1200.0)
  {
    return "FAULT";
  }

  if (
      forwardPower > 20.0 &&
      current < 1.0)
  {
    return "FAULT";
  }

  if (
      voltage < 44.0 ||
      voltage > 55.0)
  {
    return "DEGRADED";
  }

  if (current > 20.0)
  {
    return "DEGRADED";
  }

  if (power > 900.0)
  {
    return "DEGRADED";
  }

  return "NORMAL";
}

// ====================================================
// BACKHAUL
// ====================================================

float calculateLinkQuality(
    float currentRssi)
{
  if (currentRssi >= -45.0)
  {
    return 100.0;
  }

  if (currentRssi <= -120.0)
  {
    return 0.0;
  }

  return
      ((currentRssi + 120.0) /
       75.0) *
      100.0;
}

String determineBackhaulCondition(
    bool currentLinkUp,
    bool currentUpstreamReachable,
    float currentLatency,
    float currentPacketLoss,
    float currentRssi)
{
  if (
      !currentLinkUp ||
      !currentUpstreamReachable)
  {
    return "OUTAGE";
  }

  if (
      currentLatency >= 600.0 ||
      currentPacketLoss >= 60.0 ||
      currentRssi <= -95.0)
  {
    return "CRITICAL";
  }

  if (
      currentLatency >= 200.0 ||
      currentPacketLoss >= 10.0 ||
      currentRssi <= -80.0)
  {
    return "DEGRADED";
  }

  return "NORMAL";
}

// ====================================================
// LOCAL SITE STATUS
// ====================================================

String determineLocalSiteStatus(
    String currentRfHealth,
    String currentElectricalHealth,
    float currentPaTemperature,
    float currentBatterySOC,
    float currentDynamicVibration)
{
  if (
      currentRfHealth == "FAULT" ||
      currentRfHealth == "SEVERE FAULT")
  {
    return "FAULT";
  }

  if (
      currentElectricalHealth ==
      "FAULT")
  {
    return "FAULT";
  }

  if (
      currentPaTemperature >=
      80.0)
  {
    return "FAULT";
  }

  if (
      currentDynamicVibration >=
      3.0)
  {
    return "FAULT";
  }

  if (
      currentRfHealth ==
      "DEGRADED")
  {
    return "DEGRADED";
  }

  if (
      currentElectricalHealth ==
      "DEGRADED")
  {
    return "DEGRADED";
  }

  if (
      currentPaTemperature >=
      65.0)
  {
    return "DEGRADED";
  }

  if (
      currentBatterySOC <
      30.0)
  {
    return "DEGRADED";
  }

  if (
      currentDynamicVibration >=
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
// ENERGY MANAGEMENT
// ====================================================

String determineActivePowerSource(
    bool currentGridAvailable,
    bool currentGeneratorRunning,
    float currentBatterySOC)
{
  if (currentGridAvailable)
  {
    return "GRID";
  }

  if (currentGeneratorRunning)
  {
    return "GENERATOR";
  }

  if (currentBatterySOC > 5.0)
  {
    return "BATTERY";
  }

  return "NO POWER";
}

String determineEnergyAction(
    bool currentGridAvailable,
    bool currentGeneratorRunning,
    float currentBatterySOC)
{
  if (
      currentGridAvailable &&
      currentGeneratorRunning)
  {
    return "STOP_GENERATOR";
  }

  if (currentGridAvailable)
  {
    return "NORMAL_OPERATION";
  }

  if (currentGeneratorRunning)
  {
    return "GENERATOR_SUPPLY";
  }

  if (currentBatterySOC > 30.0)
  {
    return "BATTERY_BACKUP";
  }

  if (currentBatterySOC > 10.0)
  {
    return "START_GENERATOR";
  }

  if (currentBatterySOC > 5.0)
  {
    return "LOAD_SHEDDING";
  }

  return "POWER_CRITICAL";
}

// ====================================================
// BASELINE OPERATING MODE
// ====================================================

String determineOperatingMode()
{
  // Critical backup-energy condition.
  if (
      !gridAvailable &&
      !generatorRunning &&
      batterySOC <= 25.0)
  {
    operatingModeReason =
        "CRITICAL BACKUP ENERGY";

    return "EMERGENCY";
  }

  // Major faults prioritise stability and recovery.
  bool majorFault =
      (
          rfHealth == "FAULT" ||
          rfHealth == "SEVERE FAULT" ||
          electricalHealth == "FAULT" ||
          backhaulCondition == "CRITICAL" ||
          backhaulCondition == "OUTAGE" ||
          !fanOperational ||
          !rectifierNormal ||
          !radioOperational
      );

  if (majorFault)
  {
    operatingModeReason =
        "FAULT / RECOVERY PRIORITY";

    return "FULL";
  }

  // If the site is running from battery, conserve energy
  // while sufficient reserve still exists.
  if (
      !gridAvailable &&
      !generatorRunning)
  {
    operatingModeReason =
        "BATTERY CONSERVATION";

    return "ECO";
  }

  // High traffic requires full capacity.
  if (trafficLoad >= 65.0)
  {
    operatingModeReason =
        "HIGH TRAFFIC";

    return "FULL";
  }

  // Moderate load: normal energy-saving mode.
  if (trafficLoad >= 20.0)
  {
    operatingModeReason =
        "MODERATE TRAFFIC";

    return "ECO";
  }

  // Very low healthy traffic.
  operatingModeReason =
      "LOW TRAFFIC";

  return "REDUCED";
}

float getOperatingModePowerFactor(
    const String &mode)
{
  // Simulation assumptions for the first baseline.
  //
  // FULL      = 100% of baseline load
  // ECO       = 82%
  // REDUCED   = 65%
  // EMERGENCY = 50%
  //
  // These values are not production NetOne figures.
  // They are digital-twin assumptions used to compare
  // relative energy consumption.

  if (mode == "ECO")
  {
    return 0.82;
  }

  if (mode == "REDUCED")
  {
    return 0.65;
  }

  if (mode == "EMERGENCY")
  {
    return 0.50;
  }

  return 1.00;
}

void updateEnergyModeMetrics()
{
  operatingMode =
      determineOperatingMode();

  // Current DC power is used as the first simulated
  // site-power baseline/proxy.
  baselineSitePowerKW =
      dcPower /
      1000.0;

  float modeFactor =
      getOperatingModePowerFactor(
          operatingMode
      );

  managedSitePowerKW =
      baselineSitePowerKW *
      modeFactor;

  if (baselineSitePowerKW > 0.0001)
  {
    estimatedEnergySavingPct =
        (
            1.0 -
            modeFactor
        ) *
        100.0;
  }
  else
  {
    estimatedEnergySavingPct =
        0.0;
  }
}
// ====================================================
// DERIVED STATUS CALCULATIONS
// ====================================================

void recalculateSystemState()
{
  rfValid =
      isRFMeasurementValid(
          rfForwardPower,
          rfReflectedPower
      );

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
  else
  {
    vswr = 0.0;
    returnLoss = 0.0;
  }

  rfHealth =
      determineRFHealth(
          rfValid,
          vswr
      );

  electricalHealth =
      determineElectricalHealth(
          dcBusVoltage,
          dcBusCurrent,
          dcPower,
          rfForwardPower
      );

  linkQuality =
      calculateLinkQuality(
          rssi
      );

  backhaulCondition =
      determineBackhaulCondition(
          linkUp,
          upstreamReachable,
          latency,
          packetLoss,
          rssi
      );

  activePowerSource =
      determineActivePowerSource(
          gridAvailable,
          generatorRunning,
          batterySOC
      );

  energyAction =
      determineEnergyAction(
          gridAvailable,
          generatorRunning,
          batterySOC
      );

  localSiteStatus =
      determineLocalSiteStatus(
          rfHealth,
          electricalHealth,
          paTemperature,
          batterySOC,
          dynamicVibration
      );

  // Explicit local-equipment and site-power faults.
  // These are ground-truth digital-twin states.
  if (
      !gridAvailable ||
      !fanOperational ||
      !rectifierNormal ||
      !radioOperational)
  {
    localSiteStatus =
        "FAULT";
  }

  faultCandidate =
      determineFaultCandidate(
          localSiteStatus,
          backhaulCondition
      );

  updateEnergyModeMetrics();
}

// ====================================================
// FAST ANALOG + SWITCH INPUTS
// ====================================================

void readFastInputs()
{
  int dcVoltageRaw =
      analogRead(
          DC_VOLTAGE_PIN
      );

  int dcCurrentRaw =
      analogRead(
          DC_CURRENT_PIN
      );

  int batteryVoltageRaw =
      analogRead(
          BATTERY_VOLTAGE_PIN
      );

  int rfForwardRaw =
      analogRead(
          RF_FORWARD_PIN
      );

  int rfReflectedRaw =
      analogRead(
          RF_REFLECTED_PIN
      );

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

  int trafficLoadRaw =
      analogRead(
          TRAFFIC_LOAD_PIN
      );

  // --------------------------------------------------
  // ENGINEERING SCALES
  // --------------------------------------------------

  dcBusVoltage =
      (
          dcVoltageRaw /
          4095.0
      ) *
      60.0;

  dcBusCurrent =
      (
          dcCurrentRaw /
          4095.0
      ) *
      30.0;

  dcPower =
      dcBusVoltage *
      dcBusCurrent;

  batteryVoltage =
      (
          batteryVoltageRaw /
          4095.0
      ) *
      15.0;

  batterySOC =
      calculateBatterySOC(
          batteryVoltage
      );

  rfForwardPower =
      (
          rfForwardRaw /
          4095.0
      ) *
      100.0;

  rfReflectedPower =
      (
          rfReflectedRaw /
          4095.0
      ) *
      100.0;

  latency =
      10.0 +
      (
          latencyRaw /
          4095.0
      ) *
      990.0;

  packetLoss =
      (
          packetLossRaw /
          4095.0
      ) *
      100.0;

  rssi =
      -45.0 -
      (
          rssiRaw /
          4095.0
      ) *
      75.0;

  trafficLoad =
      (
          trafficLoadRaw /
          4095.0
      ) *
      100.0;

  // --------------------------------------------------
  // DIGITAL INPUTS
  // --------------------------------------------------

  linkUp =
      digitalRead(
          BACKHAUL_LINK_PIN
      ) ==
      HIGH;

  upstreamReachable =
      digitalRead(
          UPSTREAM_REACHABLE_PIN
      ) ==
      HIGH;

  gridAvailable =
      digitalRead(
          GRID_AVAILABLE_PIN
      ) ==
      HIGH;

  generatorRunning =
      digitalRead(
          GENERATOR_RUNNING_PIN
      ) ==
      HIGH;

  fanOperational =
      digitalRead(
          FAN_OPERATIONAL_PIN
      ) ==
      HIGH;

  rectifierNormal =
      digitalRead(
          RECTIFIER_NORMAL_PIN
      ) ==
      HIGH;

  radioOperational =
      digitalRead(
          RADIO_OPERATIONAL_PIN
      ) ==
      HIGH;

  recalculateSystemState();
}

// ====================================================
// VIBRATION FEATURE EXTRACTION
// ====================================================

void processVibrationSpectrum()
{
  // --------------------------------------------------
  // TIME-DOMAIN FEATURES
  // --------------------------------------------------

  double sum = 0.0;
  double sumSquares = 0.0;

  double minimumValue =
      vibrationReal[0];

  double maximumValue =
      vibrationReal[0];

  for (
      uint16_t i = 0;
      i < FFT_SAMPLES;
      i++)
  {
    double sample =
        vibrationReal[i];

    sum += sample;

    sumSquares +=
        sample *
        sample;

    if (sample < minimumValue)
    {
      minimumValue =
          sample;
    }

    if (sample > maximumValue)
    {
      maximumValue =
          sample;
    }
  }

  double mean =
      sum /
      FFT_SAMPLES;

  vibrationMean =
      mean;

  vibrationRMS =
      sqrt(
          sumSquares /
          FFT_SAMPLES
      );

  double varianceSum =
      0.0;

  for (
      uint16_t i = 0;
      i < FFT_SAMPLES;
      i++)
  {
    double deviation =
        vibrationReal[i] -
        mean;

    varianceSum +=
        deviation *
        deviation;
  }

  vibrationStdDev =
      sqrt(
          varianceSum /
          FFT_SAMPLES
      );

  vibrationPeakToPeak =
      maximumValue -
      minimumValue;

  // --------------------------------------------------
  // FREQUENCY-DOMAIN FEATURES
  // --------------------------------------------------

  for (
      uint16_t i = 0;
      i < FFT_SAMPLES;
      i++)
  {
    vibrationImag[i] =
        0.0;
  }

  // For an almost perfectly stationary signal there is
  // no useful vibration spectrum to analyse.
  if (vibrationRMS < 0.0001)
  {
    dominantVibrationFrequencyHz =
        0.0;

    vibrationSpectralEnergy =
        0.0;

    vibrationSpectralCentroidHz =
        0.0;

    vibrationSpectrumReady =
        true;

    return;
  }

  // Remove any remaining DC offset.
  vibrationFFT.dcRemoval();

  // Hamming window reduces spectral leakage caused by
  // analysing a finite-length sample window.
  vibrationFFT.windowing(
      FFTWindow::Hamming,
      FFTDirection::Forward
  );

  vibrationFFT.compute(
      FFTDirection::Forward
  );

  vibrationFFT.complexToMagnitude();

  dominantVibrationFrequencyHz =
      vibrationFFT.majorPeak();

  double spectralEnergy =
      0.0;

  double weightedFrequencySum =
      0.0;

  double magnitudeSum =
      0.0;

  // Ignore bin 0 because that is the DC component.
  for (
      uint16_t i = 1;
      i < FFT_SAMPLES / 2;
      i++)
  {
    double magnitude =
        vibrationReal[i];

    double frequency =
        (
            i *
            VIBRATION_SAMPLE_RATE_HZ
        ) /
        FFT_SAMPLES;

    spectralEnergy +=
        magnitude *
        magnitude;

    weightedFrequencySum +=
        frequency *
        magnitude;

    magnitudeSum +=
        magnitude;
  }

  vibrationSpectralEnergy =
      spectralEnergy /
      FFT_SAMPLES;

  if (magnitudeSum > 0.000001)
  {
    vibrationSpectralCentroidHz =
        weightedFrequencySum /
        magnitudeSum;
  }
  else
  {
    vibrationSpectralCentroidHz =
        0.0;
  }

  vibrationSpectrumReady =
      true;
}

// ====================================================
// MPU6050
// ====================================================

void readMPU6050()
{
  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t mpuTemp;

  mpu.getEvent(
      &accel,
      &gyro,
      &mpuTemp
  );

  accelX =
      accel.acceleration.x;

  accelY =
      accel.acceleration.y;

  accelZ =
      accel.acceleration.z;

  totalAcceleration =
      sqrt(
          accelX * accelX +
          accelY * accelY +
          accelZ * accelZ
      );

  // Signed gravity-removed vibration waveform.
  vibrationSignal =
      totalAcceleration -
      GRAVITY;

  // Absolute instantaneous magnitude retained for the
  // existing local-health rules.
  dynamicVibration =
      fabs(
          vibrationSignal
      );

  // Store one sample in the FFT window.
  vibrationReal[
      vibrationSampleIndex
  ] =
      vibrationSignal;

  vibrationImag[
      vibrationSampleIndex
  ] =
      0.0;

  vibrationSampleIndex++;

  // Once the window is full, calculate the signal
  // processing features and begin a new window.
  if (
      vibrationSampleIndex >=
      FFT_SAMPLES)
  {
    processVibrationSpectrum();

    vibrationSampleIndex =
        0;
  }

  recalculateSystemState();
}
// ====================================================
// DHT22
// ====================================================

void readDHT22()
{
  float newTemperature =
      dht.readTemperature();

  float newHumidity =
      dht.readHumidity();

  if (!isnan(newTemperature))
  {
    shelterTemperature =
        newTemperature;
  }

  if (!isnan(newHumidity))
  {
    humidity =
        newHumidity;
  }
}

// ====================================================
// NON-BLOCKING DS18B20
// ====================================================

void beginDS18B20Conversion(
    unsigned long now)
{
  paTemperatureSensor
      .requestTemperatures();

  lastDS18B20RequestTime =
      now;

  ds18b20ConversionPending =
      true;
}

void handleDS18B20(
    unsigned long now)
{
  if (
      ds18b20ConversionPending &&
      now - lastDS18B20RequestTime >=
          DS18B20_CONVERSION_MS)
  {
    float newPaTemperature =
        paTemperatureSensor
            .getTempCByIndex(0);

    if (
        newPaTemperature !=
            DEVICE_DISCONNECTED_C)
    {
      paTemperature =
          newPaTemperature;
    }

    ds18b20ConversionPending =
        false;

    recalculateSystemState();
  }

  if (
      !ds18b20ConversionPending &&
      now - lastDS18B20RequestTime >=
          DS18B20_REQUEST_INTERVAL_MS)
  {
    beginDS18B20Conversion(
        now
    );
  }
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

  lcdEnergySource.init();
  lcdTrafficLoad.init();
  lcdOperatingMode.init();

  lcdDcVoltage.backlight();
  lcdDcCurrent.backlight();
  lcdBattery.backlight();

  lcdRfForward.backlight();
  lcdRfReflected.backlight();

  lcdBackhaul.backlight();
  lcdLatency.backlight();
  lcdPacketLoss.backlight();
  lcdRssi.backlight();

  lcdEnergySource.backlight();
  lcdTrafficLoad.backlight();
  lcdOperatingMode.backlight();

  // Labels are static: write them only once.
  setLCDLabel(
      lcdDcVoltage,
      "DC BUS VOLTAGE"
  );

  setLCDLabel(
      lcdDcCurrent,
      "DC BUS CURRENT"
  );

  setLCDLabel(
      lcdBattery,
      "BATTERY VOLTAGE"
  );

  setLCDLabel(
      lcdRfForward,
      "FORWARD RF"
  );

  setLCDLabel(
      lcdRfReflected,
      "REFLECTED RF"
  );

  setLCDLabel(
      lcdBackhaul,
      "BACKHAUL STATUS"
  );

  setLCDLabel(
      lcdLatency,
      "LATENCY"
  );

  setLCDLabel(
      lcdPacketLoss,
      "PACKET LOSS"
  );

  setLCDLabel(
      lcdRssi,
      "RSSI"
  );

  setLCDLabel(
      lcdEnergySource,
      "POWER SOURCE"
  );

  setLCDLabel(
      lcdOperatingMode,
      "OPERATING MODE"
  );

  setLCDLabel(
      lcdTrafficLoad,
      "TRAFFIC LOAD"
  );

  // Initial value rows.
  writeLCDLine(
      lcdDcVoltage,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdDcCurrent,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdBattery,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdRfForward,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdRfReflected,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdBackhaul,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdLatency,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdPacketLoss,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdRssi,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdEnergySource,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdTrafficLoad,
      1,
      "Starting..."
  );

  writeLCDLine(
      lcdOperatingMode,
      1,
      "Starting..."
  );
}

// ====================================================
// RESPONSIVE LCD REFRESH
// ====================================================

void refreshLCDs(
    bool force = false)
{
  String dcVoltageText =
      String(
          dcBusVoltage,
          2
      ) +
      " V";

  String dcCurrentText =
      String(
          dcBusCurrent,
          2
      ) +
      " A";

  String batteryText =
      String(
          batteryVoltage,
          2
      ) +
      " V " +
      String(
          batterySOC,
          0
      ) +
      "%";

  String rfForwardText =
      String(
          rfForwardPower,
          2
      ) +
      " W";

  String rfReflectedText =
      String(
          rfReflectedPower,
          2
      ) +
      " W";

  String latencyText =
      String(
          latency,
          1
      ) +
      " ms";

  String packetLossText =
      String(
          packetLoss,
          1
      ) +
      " %";

  String rssiText =
      String(
          rssi,
          1
      ) +
      " dBm";

  String trafficLoadText =
      String(
          trafficLoad,
          1
      ) +
      " %";

  updateLCDValueIfChanged(
      lcdDcVoltage,
      dcVoltageText,
      lastDcVoltageLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdDcCurrent,
      dcCurrentText,
      lastDcCurrentLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdBattery,
      batteryText,
      lastBatteryLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdRfForward,
      rfForwardText,
      lastRfForwardLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdRfReflected,
      rfReflectedText,
      lastRfReflectedLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdBackhaul,
      backhaulCondition,
      lastBackhaulLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdLatency,
      latencyText,
      lastLatencyLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdPacketLoss,
      packetLossText,
      lastPacketLossLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdRssi,
      rssiText,
      lastRssiLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdEnergySource,
      activePowerSource,
      lastEnergySourceLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdTrafficLoad,
      trafficLoadText,
      lastTrafficLoadLCD,
      force
  );

  updateLCDValueIfChanged(
      lcdOperatingMode,
      operatingMode,
      lastOperatingModeLCD,
      force
  );
}

// ====================================================
// TELEMETRY
// ====================================================

void printTelemetry()
{
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

  // --------------------------------------------------
  // ENVIRONMENT
  // --------------------------------------------------

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
  Serial.println(" C");

  Serial.print(
      "Humidity            : "
  );
  Serial.print(
      humidity,
      2
  );
  Serial.println(" %");

  Serial.print(
      "PA Temperature      : "
  );
  Serial.print(
      paTemperature,
      2
  );
  Serial.println(" C");

  // --------------------------------------------------
  // VIBRATION
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ VIBRATION ]"
  );

  Serial.print(
      "Acceleration X      : "
  );
  Serial.print(
      accelX,
      2
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Acceleration Y      : "
  );
  Serial.print(
      accelY,
      2
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Acceleration Z      : "
  );
  Serial.print(
      accelZ,
      2
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Total Accel         : "
  );
  Serial.print(
      totalAcceleration,
      2
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Dynamic Vibration   : "
  );
  Serial.print(
      dynamicVibration,
      3
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Vibration RMS       : "
  );
  Serial.print(
      vibrationRMS,
      4
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Vibration Std Dev   : "
  );
  Serial.print(
      vibrationStdDev,
      4
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Vibration Peak-Peak : "
  );
  Serial.print(
      vibrationPeakToPeak,
      4
  );
  Serial.println(" m/s^2");

  Serial.print(
      "Dominant Frequency  : "
  );

  if (vibrationSpectrumReady)
  {
    Serial.print(
        dominantVibrationFrequencyHz,
        2
    );

    Serial.println(" Hz");
  }
  else
  {
    Serial.println(
        "COLLECTING"
    );
  }

  Serial.print(
      "Spectral Centroid   : "
  );

  if (vibrationSpectrumReady)
  {
    Serial.print(
        vibrationSpectralCentroidHz,
        2
    );

    Serial.println(" Hz");
  }
  else
  {
    Serial.println(
        "COLLECTING"
    );
  }

  Serial.print(
      "Spectral Energy     : "
  );

  if (vibrationSpectrumReady)
  {
    Serial.println(
        vibrationSpectralEnergy,
        6
    );
  }
  else
  {
    Serial.println(
        "COLLECTING"
    );
  }

  // --------------------------------------------------
  // DC POWER
  // --------------------------------------------------

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
  Serial.println(" V");

  Serial.print(
      "DC Bus Current      : "
  );
  Serial.print(
      dcBusCurrent,
      2
  );
  Serial.println(" A");

  Serial.print(
      "DC Power            : "
  );
  Serial.print(
      dcPower,
      2
  );
  Serial.println(" W");

  Serial.print(
      "Electrical Health   : "
  );
  Serial.println(
      electricalHealth
  );

  // --------------------------------------------------
  // BATTERY
  // --------------------------------------------------

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
  Serial.println(" V");

  Serial.print(
      "Battery SoC         : "
  );
  Serial.print(
      batterySOC,
      1
  );
  Serial.println(" %");

  // --------------------------------------------------
  // RF
  // --------------------------------------------------

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
  Serial.println(" W");

  Serial.print(
      "Reflected Power     : "
  );
  Serial.print(
      rfReflectedPower,
      2
  );
  Serial.println(" W");

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
    Serial.println(" dB");

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
  }

  Serial.print(
      "RF Health           : "
  );
  Serial.println(
      rfHealth
  );

  // --------------------------------------------------
  // BACKHAUL
  // --------------------------------------------------

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
  Serial.println(" ms");

  Serial.print(
      "Packet Loss         : "
  );
  Serial.print(
      packetLoss,
      1
  );
  Serial.println(" %");

  Serial.print(
      "RSSI                : "
  );
  Serial.print(
      rssi,
      1
  );
  Serial.println(" dBm");

  Serial.print(
      "Link Quality        : "
  );
  Serial.print(
      linkQuality,
      1
  );
  Serial.println(" %");

  Serial.print(
      "Backhaul Condition  : "
  );
  Serial.println(
      backhaulCondition
  );

  // --------------------------------------------------
  // OPERATING CONDITIONS / LOCAL EQUIPMENT
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ OPERATING CONDITIONS / LOCAL EQUIPMENT ]"
  );

  Serial.print(
      "Traffic Load        : "
  );
  Serial.print(
      trafficLoad,
      1
  );
  Serial.println(" %");

  Serial.print(
      "Cooling Fan         : "
  );
  Serial.println(
      fanOperational
          ? "OPERATIONAL"
          : "FAILED"
  );

  Serial.print(
      "Rectifier           : "
  );
  Serial.println(
      rectifierNormal
          ? "NORMAL"
          : "FAULT"
  );

  Serial.print(
      "Radio Subsystem     : "
  );
  Serial.println(
      radioOperational
          ? "OPERATIONAL"
          : "FAULT"
  );

  // --------------------------------------------------
  // ENERGY
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ POWER SOURCE / ENERGY MANAGEMENT ]"
  );

  Serial.print(
      "Grid Available      : "
  );
  Serial.println(
      gridAvailable
          ? "YES"
          : "NO"
  );

  Serial.print(
      "Generator Running   : "
  );
  Serial.println(
      generatorRunning
          ? "YES"
          : "NO"
  );

  Serial.print(
      "Active Power Source : "
  );
  Serial.println(
      activePowerSource
  );

  Serial.print(
      "Energy Action       : "
  );
  Serial.println(
      energyAction
  );

  Serial.println(
      "Action Source       : RULE-BASED ENERGY GROUND TRUTH"
  );

  Serial.println();

  Serial.println(
      "[ OPERATING MODE / ENERGY BASELINE ]"
  );

  Serial.print(
      "Operating Mode      : "
  );
  Serial.println(
      operatingMode
  );

  Serial.print(
      "Mode Reason         : "
  );
  Serial.println(
      operatingModeReason
  );

  Serial.print(
      "Baseline Site Power : "
  );
  Serial.print(
      baselineSitePowerKW,
      3
  );
  Serial.println(" kW");

  Serial.print(
      "Managed Site Power  : "
  );
  Serial.print(
      managedSitePowerKW,
      3
  );
  Serial.println(" kW");

  Serial.print(
      "Estimated Saving    : "
  );
  Serial.print(
      estimatedEnergySavingPct,
      1
  );
  Serial.println(" %");

  Serial.println(
      "Mode Source         : RULE-BASED BASELINE POLICY"
  );

  // --------------------------------------------------
  // FAULT DIFFERENTIATION
  // --------------------------------------------------

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

  // --------------------------------------------------
  // SYSTEM
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ SYSTEM ]"
  );

  Serial.println(
      "Stage               : DATA ACQUISITION + ENERGY MODELLING"
  );

  Serial.println(
      "AI Classifier       : NOT YET ACTIVE"
  );

  Serial.println(
      "Energy AI           : NOT YET ACTIVE"
  );

  Serial.println(
      "Control Refresh     : 50 ms"
  );

  Serial.println(
      "LCD Refresh         : 100 ms"
  );

  Serial.println(
      "Runtime             : NON-BLOCKING"
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

  delay(500);

  Serial.println();

  Serial.println(
      "======================================"
  );

  Serial.println(
      " AUTONOMOUS BASE STATION"
  );

  Serial.println(
      " RESPONSIVE SENSOR NODE"
  );

  Serial.println(
      "======================================"
  );

  // Explicitly lock ESP32 ADC reads to 12-bit.
  analogReadResolution(
      12
  );

  // --------------------------------------------------
  // INPUT PINS
  // --------------------------------------------------

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

  pinMode(
      BACKHAUL_LINK_PIN,
      INPUT
  );

  pinMode(
      UPSTREAM_REACHABLE_PIN,
      INPUT
  );

  pinMode(
      GRID_AVAILABLE_PIN,
      INPUT
  );

  pinMode(
      GENERATOR_RUNNING_PIN,
      INPUT
  );

  pinMode(
      TRAFFIC_LOAD_PIN,
      INPUT
  );

  pinMode(
      FAN_OPERATIONAL_PIN,
      INPUT
  );

  pinMode(
      RECTIFIER_NORMAL_PIN,
      INPUT
  );

  pinMode(
      RADIO_OPERATIONAL_PIN,
      INPUT
  );

  // --------------------------------------------------
  // I2C
  // --------------------------------------------------

  Wire.begin(
      21,
      22
  );

  // --------------------------------------------------
  // DHT22
  // --------------------------------------------------

  dht.begin();

  // --------------------------------------------------
  // MPU6050
  // --------------------------------------------------

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

  // --------------------------------------------------
  // DS18B20
  // --------------------------------------------------

  paTemperatureSensor.begin();

  // Critical responsiveness change:
  // do not block while DS18B20 performs conversion.
  paTemperatureSensor
      .setWaitForConversion(
          false
      );

  Serial.print(
      "DS18B20 devices found: "
  );

  Serial.println(
      paTemperatureSensor
          .getDeviceCount()
  );

  // --------------------------------------------------
  // LCDS
  // --------------------------------------------------

  initialiseLCDs();

  // --------------------------------------------------
  // INITIAL SENSOR READS
  // --------------------------------------------------

  readFastInputs();
  readMPU6050();
  readDHT22();

  refreshLCDs(
      true
  );

  unsigned long now =
      millis();

  beginDS18B20Conversion(
      now
  );

  lastFastInputTime =
      now;

  lastLCDRefreshTime =
      now;

  lastMPUTime =
      now;

  lastDHTTime =
      now;

  lastTelemetryTime =
      now;

  Serial.println(
      "Fast input loop ready."
  );

  Serial.println(
      "Non-blocking DS18B20 ready."
  );

  Serial.println(
      "Responsive LCD system ready."
  );

  Serial.println(
      "Sensor node ready."
  );
}

// ====================================================
// MAIN LOOP
// ====================================================

void loop()
{
  unsigned long now =
      millis();

  // ==================================================
  // FAST CONTROLS
  // ==================================================

  if (
      now - lastFastInputTime >=
      FAST_INPUT_INTERVAL_MS)
  {
    lastFastInputTime =
        now;

    readFastInputs();
  }

  // ==================================================
  // MPU6050
  // ==================================================

  if (
      now - lastMPUTime >=
      MPU_INTERVAL_MS)
  {
    lastMPUTime =
        now;

    readMPU6050();
  }

  // ==================================================
  // DHT22
  // ==================================================

  if (
      now - lastDHTTime >=
      DHT_INTERVAL_MS)
  {
    lastDHTTime =
        now;

    readDHT22();
  }

  // ==================================================
  // DS18B20
  // ==================================================

  handleDS18B20(
      now
  );

  // ==================================================
  // LCD REFRESH
  // ==================================================

  if (
      now - lastLCDRefreshTime >=
      LCD_REFRESH_INTERVAL_MS)
  {
    lastLCDRefreshTime =
        now;

    refreshLCDs();
  }

  // ==================================================
  // SERIAL TELEMETRY
  // ==================================================

  if (
      now - lastTelemetryTime >=
      TELEMETRY_INTERVAL_MS)
  {
    lastTelemetryTime =
        now;

    printTelemetry();
  }

  // No delay().
  // Loop remains free to service fast controls.
}



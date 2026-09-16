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
// POWER SIGNAL PROCESSING
// ====================================================

// Fast analogue inputs are sampled every 50 ms = 20 Hz.
//
// A 40-sample statistics window therefore covers:
//
// 40 / 20 = 2 seconds.
//
// EMA:
// y[n] = alpha*x[n] + (1-alpha)*y[n-1]
//
// A relatively small alpha suppresses short ADC fluctuations
// while still responding quickly enough for site monitoring.
constexpr uint16_t POWER_STATS_SAMPLES = 40;

const float POWER_EMA_ALPHA = 0.15f;

// Trends are calculated over approximately one second
// using the smoothed signals rather than individual samples.
const unsigned long POWER_TREND_INTERVAL_MS = 1000;

// ====================================================
// RF SIGNAL PROCESSING
// ====================================================
//
// RF forward/reflected controls are sampled every 50 ms,
// therefore the RF processing path runs at 20 Hz.
//
// The existing 40-sample rolling-statistics structure gives:
//
// 40 / 20 Hz = 2 seconds.
//
// RF health ground-truth rules continue to use the raw
// engineering measurements. These processed quantities are
// additional diagnostic / machine-learning features.
//
const float RF_SIGNAL_EMA_ALPHA = 0.15f;

const unsigned long RF_TREND_INTERVAL_MS = 1000;

// Reflection ratio:
//
// reflected / forward * 100
//
// A VSWR of approximately 2.0 corresponds to roughly
// 11.1% reflected-to-forward power ratio.
//
// Crossing this region is therefore treated as an RF
// mismatch event for diagnostic purposes.
const float RF_MISMATCH_RATIO_THRESHOLD_PCT = 11.1f;

// Keep transient RF events available for telemetry.
const unsigned long RF_EVENT_HOLD_MS = 3000;

// ====================================================
// BACKHAUL SIGNAL PROCESSING
// ====================================================

// Backhaul analogue controls are sampled by the same
// 50 ms fast-input loop = 20 samples per second.
//
// The existing 40-sample RollingStatsState therefore
// represents approximately a 2-second network window.
const float BACKHAUL_EMA_ALPHA = 0.20f;

const unsigned long BACKHAUL_TREND_INTERVAL_MS = 1000;

// Jitter is estimated from successive latency changes:
//
// jitterSample = |latency[n] - latency[n-1]|
//
// and then smoothed using EWMA.
const float LATENCY_SPIKE_DELTA_MS = 75.0f;

// Packet loss must remain high for this many samples
// before it is considered a burst.
//
// 10 samples at 20 Hz = 0.5 seconds.
const uint16_t PACKET_LOSS_BURST_SAMPLES = 10;
const float PACKET_LOSS_BURST_THRESHOLD = 10.0f;

// Sudden RSSI deterioration threshold.
const float RSSI_DROP_THRESHOLD_DB = 8.0f;

// Dual-time-scale RSSI processing.
//
// Fast EWMA:
//     reacts quickly to the current signal.
//
// Slow baseline:
//     remembers the recent normal operating level.
//
// A sudden deterioration is detected by comparing the
// current RSSI against the slow baseline rather than
// against the fast filter.
const float RSSI_SLOW_BASELINE_ALPHA = 0.02f;

// Event indicators remain visible long enough to appear
// in the 2-second telemetry stream.
const unsigned long BACKHAUL_EVENT_HOLD_MS = 3000;

// ====================================================
// THERMAL SIGNAL PROCESSING
// ====================================================
//
// DHT22 and DS18B20 measurements update roughly every
// two seconds.
//
// Thermal signals change much more slowly than vibration,
// RF or network signals, therefore stronger smoothing is
// appropriate.
//
const float THERMAL_EMA_ALPHA = 0.25f;

// Rate-of-rise thresholds.
//
// Temperatures are expressed as degrees Celsius per minute.
const float THERMAL_RISE_THRESHOLD_C_PER_MIN = 0.50f;
const float THERMAL_FAST_RISE_THRESHOLD_C_PER_MIN = 2.00f;

// These PA temperature limits match the existing
// local-site health model.
const float PA_THERMAL_HOT_C = 65.0f;
const float PA_THERMAL_CRITICAL_C = 80.0f;

struct RollingStatsState
{
  float buffer[POWER_STATS_SAMPLES];

  uint16_t index;
  uint16_t count;

  double sum;
  double sumSquares;

  float mean;
  float stdDev;
};

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

// ----------------------------------------------------
// Processed thermal features
// ----------------------------------------------------

float shelterTemperatureEMA = 0.0;
float humidityEMA = 0.0;
float paTemperatureEMA = 0.0;

float shelterTemperatureTrendCPerMin = 0.0;
float paTemperatureTrendCPerMin = 0.0;

// Temperature difference between the RF power amplifier
// and the shelter environment.
//
// A growing positive differential can indicate localised
// equipment heating.
float paShelterTemperatureDeltaC = 0.0;

// Strongest positive thermal rate currently observed.
float maximumThermalRiseCPerMin = 0.0;

RollingStatsState shelterTemperatureStats = {};
RollingStatsState humidityStats = {};
RollingStatsState paTemperatureStats = {};

bool shelterThermalInitialised = false;
bool humidityThermalInitialised = false;
bool paThermalInitialised = false;

unsigned long lastShelterThermalTime = 0;
unsigned long lastPaThermalTime = 0;

String thermalTrendState = "COLLECTING";
String thermalRiskState = "COLLECTING";

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

// ----------------------------------------------------
// Raw circuit-control readings
// ----------------------------------------------------
//
// Wokwi potentiometers visually use a 0-1023 position.
// The ESP32 reads them using its 12-bit ADC: 0-4095.
//
// Keeping the raw ADC values allows telemetry to show both
// the physical circuit control position and the resulting
// engineering measurement.
//
int dcVoltageRaw = 0;
int dcCurrentRaw = 0;
int batteryVoltageRaw = 0;

int rfForwardRaw = 0;
int rfReflectedRaw = 0;

int latencyRaw = 0;
int packetLossRaw = 0;
int rssiRaw = 0;

int trafficLoadRaw = 0;

// DC power
float dcBusVoltage = 0.0;
float dcBusCurrent = 0.0;
float dcPower = 0.0;

// ----------------------------------------------------
// Processed DC power features
// ----------------------------------------------------

float dcBusVoltageEMA = 0.0;
float dcBusCurrentEMA = 0.0;
float dcPowerEMA = 0.0;

float dcVoltageTrendVPerSec = 0.0;
float dcCurrentTrendAPerSec = 0.0;
float dcPowerTrendWPerSec = 0.0;

RollingStatsState dcVoltageStats = {};
RollingStatsState dcCurrentStats = {};
RollingStatsState dcPowerStats = {};

// Battery
float batteryVoltage = 0.0;
float batterySOC = 0.0;

// ----------------------------------------------------
// Processed battery features
// ----------------------------------------------------

float batteryVoltageEMA = 0.0;
float batterySOCEMA = 0.0;

float batteryVoltageTrendVPerMin = 0.0;
float batterySOCTrendPctPerMin = 0.0;

// Positive value means estimated discharge.
// A charging battery gives zero discharge rate here.
float batteryDischargeRatePctPerMin = 0.0;

String batteryTrendState = "STABLE";

RollingStatsState batteryVoltageStats = {};
RollingStatsState batterySOCStats = {};

bool powerSignalProcessingInitialised = false;

unsigned long lastPowerTrendTime = 0;

float previousDcVoltageEMA = 0.0;
float previousDcCurrentEMA = 0.0;
float previousDcPowerEMA = 0.0;

float previousBatteryVoltageEMA = 0.0;
float previousBatterySOCEMA = 0.0;

// RF
float rfForwardPower = 0.0;
float rfReflectedPower = 0.0;

bool rfValid = false;

float vswr = 0.0;
float returnLoss = 0.0;

String rfHealth = "UNKNOWN";

// ----------------------------------------------------
// Processed RF features
// ----------------------------------------------------

// Smoothed RF powers.
float rfForwardPowerEMA = 0.0;
float rfReflectedPowerEMA = 0.0;

// Reflection ratio:
//
// Pr / Pf * 100
//
// Lower is normally better.
float rfReflectionRatioPct = 0.0;
float rfReflectionRatioPctEMA = 0.0;

// Smoothed derived RF quantities.
float rfVswrEMA = 0.0;
float rfReturnLossEMA = 0.0;

// One-second trends.
float rfForwardTrendWPerSec = 0.0;
float rfReflectedTrendWPerSec = 0.0;
float rfReflectionRatioTrendPctPerSec = 0.0;
float rfVswrTrendPerSec = 0.0;
float rfReturnLossTrendDbPerSec = 0.0;

// Two-second rolling variability.
RollingStatsState rfForwardStats = {};
RollingStatsState rfReflectedStats = {};
RollingStatsState rfReflectionRatioStats = {};

bool rfSignalProcessingInitialised = false;
bool previousRfDerivedValid = false;

unsigned long lastRfTrendTime = 0;

float previousRfForwardEMA = 0.0;
float previousRfReflectedEMA = 0.0;
float previousRfReflectionRatioEMA = 0.0;
float previousRfVswrEMA = 0.0;
float previousRfReturnLossEMA = 0.0;

// RF mismatch transient / state-change capture.
bool rfMismatchDetected = false;
bool rfMismatchLatched = false;

unsigned long lastRfMismatchTime = 0;

// Backhaul
float latency = 0.0;
float packetLoss = 0.0;
float rssi = 0.0;
float linkQuality = 0.0;

bool linkUp = false;
bool upstreamReachable = false;

String backhaulCondition = "UNKNOWN";

// ----------------------------------------------------
// Processed backhaul features
// ----------------------------------------------------

float latencyEMA = 0.0;
float latencyJitterEWMA = 0.0;
float latencyTrendMsPerSec = 0.0;

float packetLossEMA = 0.0;
float packetLossTrendPctPerSec = 0.0;

float rssiEMA = 0.0;

// Slow-moving reference representing the previously
// established normal RSSI level.
float rssiSlowBaselineEMA = 0.0;

// Positive value means current RSSI is weaker than
// the slow baseline by this many dB.
float rssiDropMagnitudeDb = 0.0;

float rssiTrendDbPerSec = 0.0;

RollingStatsState latencyStats = {};
RollingStatsState packetLossStats = {};
RollingStatsState rssiStats = {};

bool backhaulSignalProcessingInitialised = false;

float previousLatencyRaw = 0.0;
float previousLatencyEMA = 0.0;
float previousPacketLossEMA = 0.0;
float previousRssiEMA = 0.0;

unsigned long lastBackhaulTrendTime = 0;

// Transient-event detection
bool latencySpikeDetected = false;
bool packetLossBurstDetected = false;
bool rssiDropDetected = false;

unsigned long lastLatencySpikeTime = 0;
unsigned long lastPacketLossBurstTime = 0;
unsigned long lastRssiDropTime = 0;

uint16_t consecutiveHighLossSamples = 0;

// ----------------------------------------------------
// BACKHAUL EVENT LATCHES
// ----------------------------------------------------
//
// These remain TRUE until the next telemetry report
// acknowledges them.
//
// This prevents short events from disappearing between
// the fast 50 ms processing loop and the slower 2-second
// human-readable telemetry output.
bool latencySpikeLatched = false;
bool packetLossBurstLatched = false;
bool rssiDropLatched = false;

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

          // Critical PA thermal fault:
          // energy saving must never take priority over
          // equipment protection and fault recovery.
          paTemperature >= PA_THERMAL_CRITICAL_C ||

          // Severe mechanical vibration is also treated
          // as a major local equipment fault.
          dynamicVibration >= 3.0f ||

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
// POWER / BATTERY SIGNAL PROCESSING
// ====================================================

// Rolling statistics provide short-term mean and variation.
//
// Standard deviation is particularly useful because a signal
// may have an acceptable average while still being unstable.
void updateRollingStats(
    RollingStatsState &state,
    float sample)
{
  if (
      state.count <
      POWER_STATS_SAMPLES)
  {
    state.buffer[
        state.index
    ] =
        sample;

    state.sum +=
        sample;

    state.sumSquares +=
        (
            double
        )sample *
        sample;

    state.count++;
  }
  else
  {
    float oldSample =
        state.buffer[
            state.index
        ];

    state.sum -=
        oldSample;

    state.sumSquares -=
        (
            double
        )oldSample *
        oldSample;

    state.buffer[
        state.index
    ] =
        sample;

    state.sum +=
        sample;

    state.sumSquares +=
        (
            double
        )sample *
        sample;
  }

  state.index++;

  if (
      state.index >=
      POWER_STATS_SAMPLES)
  {
    state.index =
        0;
  }

  if (state.count == 0)
  {
    state.mean =
        0.0;

    state.stdDev =
        0.0;

    return;
  }

  state.mean =
      state.sum /
      state.count;

  double variance =
      (
          state.sumSquares /
          state.count
      ) -
      (
          (
              double
          )state.mean *
          state.mean
      );

  // Floating point rounding may produce a tiny
  // negative variance such as -0.00000001.
  if (variance < 0.0)
  {
    variance =
        0.0;
  }

  state.stdDev =
      sqrt(
          variance
      );
}


// Exponential Moving Average.
//
// This is an IIR low-pass filter:
//
// filtered[n] =
// alpha*raw[n] +
// (1-alpha)*filtered[n-1]
float applyEMA(
    float previousFiltered,
    float newSample)
{
  return
      POWER_EMA_ALPHA *
      newSample +
      (
          1.0 -
          POWER_EMA_ALPHA
      ) *
      previousFiltered;
}


// Updates the mathematical features used later by
// diagnostics, dataset generation and machine learning.
//
// IMPORTANT:
//
// Existing ground-truth safety rules continue to use the
// original engineering measurements. These processed signals
// are additional features rather than replacements.
void updatePowerSignalProcessing(
    unsigned long now)
{
  // --------------------------------------------------
  // INITIAL CONDITION
  // --------------------------------------------------

  if (
      !powerSignalProcessingInitialised)
  {
    dcBusVoltageEMA =
        dcBusVoltage;

    dcBusCurrentEMA =
        dcBusCurrent;

    dcPowerEMA =
        dcPower;

    batteryVoltageEMA =
        batteryVoltage;

    batterySOCEMA =
        batterySOC;

    previousDcVoltageEMA =
        dcBusVoltageEMA;

    previousDcCurrentEMA =
        dcBusCurrentEMA;

    previousDcPowerEMA =
        dcPowerEMA;

    previousBatteryVoltageEMA =
        batteryVoltageEMA;

    previousBatterySOCEMA =
        batterySOCEMA;

    lastPowerTrendTime =
        now;

    powerSignalProcessingInitialised =
        true;
  }
  else
  {
    // ------------------------------------------------
    // EMA LOW-PASS FILTERING
    // ------------------------------------------------

    dcBusVoltageEMA =
        applyEMA(
            dcBusVoltageEMA,
            dcBusVoltage
        );

    dcBusCurrentEMA =
        applyEMA(
            dcBusCurrentEMA,
            dcBusCurrent
        );

    dcPowerEMA =
        applyEMA(
            dcPowerEMA,
            dcPower
        );

    batteryVoltageEMA =
        applyEMA(
            batteryVoltageEMA,
            batteryVoltage
        );

    batterySOCEMA =
        applyEMA(
            batterySOCEMA,
            batterySOC
        );
  }

  // --------------------------------------------------
  // ROLLING VARIABILITY
  // --------------------------------------------------

  updateRollingStats(
      dcVoltageStats,
      dcBusVoltage
  );

  updateRollingStats(
      dcCurrentStats,
      dcBusCurrent
  );

  updateRollingStats(
      dcPowerStats,
      dcPower
  );

  updateRollingStats(
      batteryVoltageStats,
      batteryVoltage
  );

  updateRollingStats(
      batterySOCStats,
      batterySOC
  );

  // --------------------------------------------------
  // RATE-OF-CHANGE / TREND FEATURES
  // --------------------------------------------------

  unsigned long elapsed =
      now -
      lastPowerTrendTime;

  if (
      elapsed >=
      POWER_TREND_INTERVAL_MS)
  {
    float elapsedSeconds =
        elapsed /
        1000.0;

    if (elapsedSeconds > 0.0)
    {
      dcVoltageTrendVPerSec =
          (
              dcBusVoltageEMA -
              previousDcVoltageEMA
          ) /
          elapsedSeconds;

      dcCurrentTrendAPerSec =
          (
              dcBusCurrentEMA -
              previousDcCurrentEMA
          ) /
          elapsedSeconds;

      dcPowerTrendWPerSec =
          (
              dcPowerEMA -
              previousDcPowerEMA
          ) /
          elapsedSeconds;

      // Battery changes are normally much slower,
      // therefore battery rates are expressed per minute.
      batteryVoltageTrendVPerMin =
          (
              (
                  batteryVoltageEMA -
                  previousBatteryVoltageEMA
              ) /
              elapsedSeconds
          ) *
          60.0;

      batterySOCTrendPctPerMin =
          (
              (
                  batterySOCEMA -
                  previousBatterySOCEMA
              ) /
              elapsedSeconds
          ) *
          60.0;

      if (
          batterySOCTrendPctPerMin <
          0.0)
      {
        batteryDischargeRatePctPerMin =
            -batterySOCTrendPctPerMin;
      }
      else
      {
        batteryDischargeRatePctPerMin =
            0.0;
      }

      // Small movements are treated as stable.
      if (
          batterySOCTrendPctPerMin <
          -0.05)
      {
        batteryTrendState =
            "DISCHARGING";
      }
      else if (
          batterySOCTrendPctPerMin >
          0.05)
      {
        batteryTrendState =
            "CHARGING";
      }
      else
      {
        batteryTrendState =
            "STABLE";
      }
    }

    previousDcVoltageEMA =
        dcBusVoltageEMA;

    previousDcCurrentEMA =
        dcBusCurrentEMA;

    previousDcPowerEMA =
        dcPowerEMA;

    previousBatteryVoltageEMA =
        batteryVoltageEMA;

    previousBatterySOCEMA =
        batterySOCEMA;

    lastPowerTrendTime =
        now;
  }
}


// ====================================================
// RF SIGNAL PROCESSING
// ====================================================

float applyRfEMA(
    float previousFiltered,
    float newSample)
{
  return
      RF_SIGNAL_EMA_ALPHA *
      newSample +
      (
          1.0f -
          RF_SIGNAL_EMA_ALPHA
      ) *
      previousFiltered;
}


void updateRFSignalProcessing(
    unsigned long now)
{
  // Capture the previous detector state so a genuinely
  // new mismatch can be latched on its rising edge.
  bool wasRfMismatchDetected =
      rfMismatchDetected;

  // --------------------------------------------------
  // REFLECTION RATIO
  // --------------------------------------------------
  //
  // Reflection ratio is intentionally bounded to 100%.
  //
  // If reflected power equals/exceeds forward power the
  // normal RF-validity logic already marks the measurement
  // as invalid / severe fault. Bounding the feature prevents
  // extreme ratios from dominating later ML scaling.
  if (rfForwardPower > 0.01f)
  {
    rfReflectionRatioPct =
        (
            rfReflectedPower /
            rfForwardPower
        ) *
        100.0f;

    if (rfReflectionRatioPct < 0.0f)
    {
      rfReflectionRatioPct =
          0.0f;
    }

    if (rfReflectionRatioPct > 100.0f)
    {
      rfReflectionRatioPct =
          100.0f;
    }
  }
  else
  {
    rfReflectionRatioPct =
        100.0f;
  }

  // --------------------------------------------------
  // INITIAL CONDITION
  // --------------------------------------------------

  if (!rfSignalProcessingInitialised)
  {
    rfForwardPowerEMA =
        rfForwardPower;

    rfReflectedPowerEMA =
        rfReflectedPower;

    rfReflectionRatioPctEMA =
        rfReflectionRatioPct;

    if (rfValid)
    {
      rfVswrEMA =
          vswr;

      rfReturnLossEMA =
          returnLoss;
    }
    else
    {
      rfVswrEMA =
          0.0f;

      rfReturnLossEMA =
          0.0f;
    }

    previousRfForwardEMA =
        rfForwardPowerEMA;

    previousRfReflectedEMA =
        rfReflectedPowerEMA;

    previousRfReflectionRatioEMA =
        rfReflectionRatioPctEMA;

    previousRfVswrEMA =
        rfVswrEMA;

    previousRfReturnLossEMA =
        rfReturnLossEMA;

    previousRfDerivedValid =
        rfValid;

    lastRfTrendTime =
        now;

    rfSignalProcessingInitialised =
        true;
  }
  else
  {
    // ------------------------------------------------
    // EMA FILTERING
    // ------------------------------------------------

    rfForwardPowerEMA =
        applyRfEMA(
            rfForwardPowerEMA,
            rfForwardPower
        );

    rfReflectedPowerEMA =
        applyRfEMA(
            rfReflectedPowerEMA,
            rfReflectedPower
        );

    rfReflectionRatioPctEMA =
        applyRfEMA(
            rfReflectionRatioPctEMA,
            rfReflectionRatioPct
        );

    // VSWR and return loss only have mathematical meaning
    // while the RF power pair is valid.
    if (rfValid)
    {
      if (!previousRfDerivedValid)
      {
        // Reinitialise derived filters after recovering
        // from an invalid RF measurement so a fake trend
        // is not produced.
        rfVswrEMA =
            vswr;

        rfReturnLossEMA =
            returnLoss;
      }
      else
      {
        rfVswrEMA =
            applyRfEMA(
                rfVswrEMA,
                vswr
            );

        rfReturnLossEMA =
            applyRfEMA(
                rfReturnLossEMA,
                returnLoss
            );
      }
    }
  }

  // --------------------------------------------------
  // ROLLING STATISTICS
  // --------------------------------------------------

  updateRollingStats(
      rfForwardStats,
      rfForwardPower
  );

  updateRollingStats(
      rfReflectedStats,
      rfReflectedPower
  );

  updateRollingStats(
      rfReflectionRatioStats,
      rfReflectionRatioPct
  );

  // --------------------------------------------------
  // RF MISMATCH EVENT DETECTION
  // --------------------------------------------------
  //
  // A mismatch event is produced when:
  //
  // 1. the RF pair becomes mathematically invalid, OR
  // 2. reflected/forward ratio reaches the approximate
  //    VSWR >= 2 region.
  //
  // Existing RFHealth remains the official rule-based
  // ground-truth classification.
  bool mismatchCondition =
      (
          !rfValid ||
          rfReflectionRatioPct >=
              RF_MISMATCH_RATIO_THRESHOLD_PCT
      );

  if (mismatchCondition)
  {
    lastRfMismatchTime =
        now;
  }

  rfMismatchDetected =
      (
          lastRfMismatchTime != 0 &&
          now -
          lastRfMismatchTime <=
          RF_EVENT_HOLD_MS
      );

  if (
      rfMismatchDetected &&
      !wasRfMismatchDetected)
  {
    rfMismatchLatched =
        true;
  }

  // --------------------------------------------------
  // RATE-OF-CHANGE / TREND FEATURES
  // --------------------------------------------------

  unsigned long elapsed =
      now -
      lastRfTrendTime;

  if (
      elapsed >=
      RF_TREND_INTERVAL_MS)
  {
    float elapsedSeconds =
        elapsed /
        1000.0f;

    if (elapsedSeconds > 0.0f)
    {
      rfForwardTrendWPerSec =
          (
              rfForwardPowerEMA -
              previousRfForwardEMA
          ) /
          elapsedSeconds;

      rfReflectedTrendWPerSec =
          (
              rfReflectedPowerEMA -
              previousRfReflectedEMA
          ) /
          elapsedSeconds;

      rfReflectionRatioTrendPctPerSec =
          (
              rfReflectionRatioPctEMA -
              previousRfReflectionRatioEMA
          ) /
          elapsedSeconds;

      if (
          rfValid &&
          previousRfDerivedValid)
      {
        rfVswrTrendPerSec =
            (
                rfVswrEMA -
                previousRfVswrEMA
            ) /
            elapsedSeconds;

        rfReturnLossTrendDbPerSec =
            (
                rfReturnLossEMA -
                previousRfReturnLossEMA
            ) /
            elapsedSeconds;
      }
      else
      {
        rfVswrTrendPerSec =
            0.0f;

        rfReturnLossTrendDbPerSec =
            0.0f;
      }
    }

    previousRfForwardEMA =
        rfForwardPowerEMA;

    previousRfReflectedEMA =
        rfReflectedPowerEMA;

    previousRfReflectionRatioEMA =
        rfReflectionRatioPctEMA;

    if (rfValid)
    {
      previousRfVswrEMA =
          rfVswrEMA;

      previousRfReturnLossEMA =
          rfReturnLossEMA;
    }

    previousRfDerivedValid =
        rfValid;

    lastRfTrendTime =
        now;
  }
}


// ====================================================
// BACKHAUL SIGNAL PROCESSING
// ====================================================

float applyBackhaulEMA(
    float previousFiltered,
    float newSample)
{
  return
      BACKHAUL_EMA_ALPHA *
      newSample +
      (
          1.0 -
          BACKHAUL_EMA_ALPHA
      ) *
      previousFiltered;
}


void updateBackhaulSignalProcessing(
    unsigned long now)
{
  // Capture previous event states so we can identify
  // the rising edge of a new transient event.
  bool wasLatencySpikeDetected =
      latencySpikeDetected;

  bool wasPacketLossBurstDetected =
      packetLossBurstDetected;

  bool wasRssiDropDetected =
      rssiDropDetected;

  // --------------------------------------------------
  // INITIALISE FILTERS FROM FIRST VALID SAMPLE
  // --------------------------------------------------

  if (
      !backhaulSignalProcessingInitialised)
  {
    latencyEMA =
        latency;

    packetLossEMA =
        packetLoss;

    rssiEMA =
        rssi;

    rssiSlowBaselineEMA =
        rssi;

    rssiDropMagnitudeDb =
        0.0f;

    previousLatencyRaw =
        latency;

    previousLatencyEMA =
        latencyEMA;

    previousPacketLossEMA =
        packetLossEMA;

    previousRssiEMA =
        rssiEMA;

    lastBackhaulTrendTime =
        now;

    backhaulSignalProcessingInitialised =
        true;
  }
  else
  {
    // ------------------------------------------------
    // LATENCY JITTER
    // ------------------------------------------------

    float jitterSample =
        fabs(
            latency -
            previousLatencyRaw
        );

    latencyJitterEWMA =
        applyBackhaulEMA(
            latencyJitterEWMA,
            jitterSample
        );

    // ------------------------------------------------
    // EWMA FILTERS
    // ------------------------------------------------

    latencyEMA =
        applyBackhaulEMA(
            latencyEMA,
            latency
        );

    packetLossEMA =
        applyBackhaulEMA(
            packetLossEMA,
            packetLoss
        );

    rssiEMA =
        applyBackhaulEMA(
            rssiEMA,
            rssi
        );

    // Slow baseline intentionally reacts much more slowly
    // than the normal RSSI EWMA.
    //
    // This allows a deterioration that develops over several
    // 50 ms samples to remain visible as a transient event.
    rssiSlowBaselineEMA =
        RSSI_SLOW_BASELINE_ALPHA *
        rssi +
        (
            1.0f -
            RSSI_SLOW_BASELINE_ALPHA
        ) *
        rssiSlowBaselineEMA;
  }

  // --------------------------------------------------
  // ROLLING STATISTICS
  // --------------------------------------------------

  updateRollingStats(
      latencyStats,
      latency
  );

  updateRollingStats(
      packetLossStats,
      packetLoss
  );

  updateRollingStats(
      rssiStats,
      rssi
  );

  // --------------------------------------------------
  // LATENCY SPIKE DETECTION
  // --------------------------------------------------

  if (
      latency >
      latencyEMA +
      LATENCY_SPIKE_DELTA_MS)
  {
    lastLatencySpikeTime =
        now;
  }

  latencySpikeDetected =
      (
          lastLatencySpikeTime != 0 &&
          now -
          lastLatencySpikeTime <=
          BACKHAUL_EVENT_HOLD_MS
      );

  if (
      latencySpikeDetected &&
      !wasLatencySpikeDetected)
  {
    latencySpikeLatched =
        true;
  }

  // --------------------------------------------------
  // PACKET-LOSS BURST DETECTION
  // --------------------------------------------------

  if (
      packetLoss >=
      PACKET_LOSS_BURST_THRESHOLD)
  {
    if (
        consecutiveHighLossSamples <
        65535)
    {
      consecutiveHighLossSamples++;
    }
  }
  else
  {
    consecutiveHighLossSamples =
        0;
  }

  if (
      consecutiveHighLossSamples >=
      PACKET_LOSS_BURST_SAMPLES)
  {
    lastPacketLossBurstTime =
        now;
  }

  packetLossBurstDetected =
      (
          lastPacketLossBurstTime != 0 &&
          now -
          lastPacketLossBurstTime <=
          BACKHAUL_EVENT_HOLD_MS
      );

  if (
      packetLossBurstDetected &&
      !wasPacketLossBurstDetected)
  {
    packetLossBurstLatched =
        true;
  }

  // --------------------------------------------------
  // RSSI SUDDEN-DROP DETECTION
  // --------------------------------------------------
  //
  // We compare the current RSSI against a slow baseline.
  //
  // Example:
  //
  // slow baseline = -55 dBm
  // current RSSI  = -90 dBm
  //
  // drop magnitude:
  //
  // -55 - (-90) = 35 dB
  //
  // This dual-time-scale method works even when the
  // Wokwi potentiometer changes over several samples.
  // --------------------------------------------------

  rssiDropMagnitudeDb =
      rssiSlowBaselineEMA -
      rssi;

  if (rssiDropMagnitudeDb < 0.0f)
  {
    rssiDropMagnitudeDb =
        0.0f;
  }

  if (
      rssiDropMagnitudeDb >=
      RSSI_DROP_THRESHOLD_DB)
  {
    lastRssiDropTime =
        now;
  }

  rssiDropDetected =
      (
          lastRssiDropTime != 0 &&
          now -
          lastRssiDropTime <=
          BACKHAUL_EVENT_HOLD_MS
      );

  if (
      rssiDropDetected &&
      !wasRssiDropDetected)
  {
    rssiDropLatched =
        true;
  }

  // --------------------------------------------------
  // TREND / RATE-OF-CHANGE FEATURES
  // --------------------------------------------------

  unsigned long elapsed =
      now -
      lastBackhaulTrendTime;

  if (
      elapsed >=
      BACKHAUL_TREND_INTERVAL_MS)
  {
    float elapsedSeconds =
        elapsed /
        1000.0f;

    if (elapsedSeconds > 0.0f)
    {
      latencyTrendMsPerSec =
          (
              latencyEMA -
              previousLatencyEMA
          ) /
          elapsedSeconds;

      packetLossTrendPctPerSec =
          (
              packetLossEMA -
              previousPacketLossEMA
          ) /
          elapsedSeconds;

      rssiTrendDbPerSec =
          (
              rssiEMA -
              previousRssiEMA
          ) /
          elapsedSeconds;
    }

    previousLatencyEMA =
        latencyEMA;

    previousPacketLossEMA =
        packetLossEMA;

    previousRssiEMA =
        rssiEMA;

    lastBackhaulTrendTime =
        now;
  }

  previousLatencyRaw =
      latency;
}


// ====================================================
// FAST ANALOG + SWITCH INPUTS
// ====================================================

void readFastInputs()
{
  dcVoltageRaw =
      analogRead(
          DC_VOLTAGE_PIN
      );

  dcCurrentRaw =
      analogRead(
          DC_CURRENT_PIN
      );

  batteryVoltageRaw =
      analogRead(
          BATTERY_VOLTAGE_PIN
      );

  rfForwardRaw =
      analogRead(
          RF_FORWARD_PIN
      );

  rfReflectedRaw =
      analogRead(
          RF_REFLECTED_PIN
      );

  latencyRaw =
      analogRead(
          BACKHAUL_LATENCY_PIN
      );

  packetLossRaw =
      analogRead(
          BACKHAUL_LOSS_PIN
      );

  rssiRaw =
      analogRead(
          BACKHAUL_RSSI_PIN
      );

  trafficLoadRaw =
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
  // SIGNAL PROCESSING
  // --------------------------------------------------

  unsigned long processingNow =
      millis();

  updatePowerSignalProcessing(
      processingNow
  );

  updateBackhaulSignalProcessing(
      processingNow
  );

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

  // RF signal processing is performed after
  // recalculateSystemState() so rfValid, VSWR and
  // return loss correspond to the current raw RF sample.
  //
  // Processed RF features DO NOT replace the existing
  // rule-based RF ground truth.
  updateRFSignalProcessing(
      processingNow
  );
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
// THERMAL SIGNAL PROCESSING
// ====================================================

float applyThermalEMA(
    float previousFiltered,
    float newSample)
{
  return
      THERMAL_EMA_ALPHA *
      newSample +
      (
          1.0f -
          THERMAL_EMA_ALPHA
      ) *
      previousFiltered;
}


// Recalculate derived thermal relationships after either
// shelter or PA temperature changes.
void updateThermalDerivedState()
{
  if (
      shelterThermalInitialised &&
      paThermalInitialised)
  {
    paShelterTemperatureDeltaC =
        paTemperatureEMA -
        shelterTemperatureEMA;
  }
  else
  {
    paShelterTemperatureDeltaC =
        0.0f;
  }

  maximumThermalRiseCPerMin =
      shelterTemperatureTrendCPerMin;

  if (
      paTemperatureTrendCPerMin >
      maximumThermalRiseCPerMin)
  {
    maximumThermalRiseCPerMin =
        paTemperatureTrendCPerMin;
  }

  // --------------------------------------------------
  // THERMAL TREND STATE
  // --------------------------------------------------

  if (
      !shelterThermalInitialised ||
      !paThermalInitialised)
  {
    thermalTrendState =
        "COLLECTING";
  }
  else if (
      maximumThermalRiseCPerMin >=
      THERMAL_FAST_RISE_THRESHOLD_C_PER_MIN)
  {
    thermalTrendState =
        "RISING_FAST";
  }
  else if (
      maximumThermalRiseCPerMin >=
      THERMAL_RISE_THRESHOLD_C_PER_MIN)
  {
    thermalTrendState =
        "RISING";
  }
  else if (
      shelterTemperatureTrendCPerMin <=
          -THERMAL_RISE_THRESHOLD_C_PER_MIN &&
      paTemperatureTrendCPerMin <=
          -THERMAL_RISE_THRESHOLD_C_PER_MIN)
  {
    thermalTrendState =
        "FALLING";
  }
  else
  {
    thermalTrendState =
        "STABLE";
  }

  // --------------------------------------------------
  // THERMAL RISK STATE
  //
  // This is diagnostic information only.
  // Existing rule-based ground-truth decisions are not
  // replaced by this processed value.
  // --------------------------------------------------

  if (!paThermalInitialised)
  {
    thermalRiskState =
        "COLLECTING";
  }
  else if (
      paTemperatureEMA >=
      PA_THERMAL_CRITICAL_C)
  {
    thermalRiskState =
        "CRITICAL";
  }
  else if (
      paTemperatureEMA >=
      PA_THERMAL_HOT_C)
  {
    thermalRiskState =
        "HOT";
  }
  else if (
      maximumThermalRiseCPerMin >=
      THERMAL_FAST_RISE_THRESHOLD_C_PER_MIN)
  {
    thermalRiskState =
        "FAST_RISE";
  }
  else
  {
    thermalRiskState =
        "NORMAL";
  }
}


// Process DHT22 shelter temperature and humidity.
void updateShelterThermalProcessing(
    unsigned long now,
    bool temperatureUpdated,
    bool humidityUpdated)
{
  if (temperatureUpdated)
  {
    updateRollingStats(
        shelterTemperatureStats,
        shelterTemperature
    );

    if (!shelterThermalInitialised)
    {
      shelterTemperatureEMA =
          shelterTemperature;

      shelterTemperatureTrendCPerMin =
          0.0f;

      lastShelterThermalTime =
          now;

      shelterThermalInitialised =
          true;
    }
    else
    {
      float previousEMA =
          shelterTemperatureEMA;

      shelterTemperatureEMA =
          applyThermalEMA(
              shelterTemperatureEMA,
              shelterTemperature
          );

      unsigned long elapsed =
          now -
          lastShelterThermalTime;

      if (elapsed > 0)
      {
        float elapsedMinutes =
            elapsed /
            60000.0f;

        if (elapsedMinutes > 0.0f)
        {
          shelterTemperatureTrendCPerMin =
              (
                  shelterTemperatureEMA -
                  previousEMA
              ) /
              elapsedMinutes;
        }
      }

      lastShelterThermalTime =
          now;
    }
  }

  if (humidityUpdated)
  {
    updateRollingStats(
        humidityStats,
        humidity
    );

    if (!humidityThermalInitialised)
    {
      humidityEMA =
          humidity;

      humidityThermalInitialised =
          true;
    }
    else
    {
      humidityEMA =
          applyThermalEMA(
              humidityEMA,
              humidity
          );
    }
  }

  updateThermalDerivedState();
}


// Process DS18B20 PA temperature.
void updatePaThermalProcessing(
    unsigned long now)
{
  updateRollingStats(
      paTemperatureStats,
      paTemperature
  );

  if (!paThermalInitialised)
  {
    paTemperatureEMA =
        paTemperature;

    paTemperatureTrendCPerMin =
        0.0f;

    lastPaThermalTime =
        now;

    paThermalInitialised =
        true;
  }
  else
  {
    float previousEMA =
        paTemperatureEMA;

    paTemperatureEMA =
        applyThermalEMA(
            paTemperatureEMA,
            paTemperature
        );

    unsigned long elapsed =
        now -
        lastPaThermalTime;

    if (elapsed > 0)
    {
      float elapsedMinutes =
          elapsed /
          60000.0f;

      if (elapsedMinutes > 0.0f)
      {
        paTemperatureTrendCPerMin =
            (
                paTemperatureEMA -
                previousEMA
            ) /
            elapsedMinutes;
      }
    }

    lastPaThermalTime =
        now;
  }

  updateThermalDerivedState();
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

  bool temperatureUpdated =
      false;

  bool humidityUpdated =
      false;

  if (!isnan(newTemperature))
  {
    shelterTemperature =
        newTemperature;

    temperatureUpdated =
        true;
  }

  if (!isnan(newHumidity))
  {
    humidity =
        newHumidity;

    humidityUpdated =
        true;
  }

  if (
      temperatureUpdated ||
      humidityUpdated)
  {
    updateShelterThermalProcessing(
        millis(),
        temperatureUpdated,
        humidityUpdated
    );
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

      updatePaThermalProcessing(
          now
      );
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
// CIRCUIT TELEMETRY HELPERS
// ====================================================

// Convert the ESP32 12-bit ADC reading back to the
// approximate 0-1023 control value displayed by a
// Wokwi potentiometer.
int adcToWokwiPotValue(
    int rawAdc)
{
  if (rawAdc < 0)
  {
    rawAdc = 0;
  }

  if (rawAdc > 4095)
  {
    rawAdc = 4095;
  }

  return
      (
          (
              (long)rawAdc *
              1023L
          ) +
          2047L
      ) /
      4095L;
}


void printCircuitPotLine(
    const char *label,
    int rawAdc,
    float engineeringValue,
    uint8_t decimals,
    const char *unit)
{
  Serial.print(label);
  Serial.print(" : ");

  Serial.print(
      adcToWokwiPotValue(
          rawAdc
      )
  );

  Serial.print("/1023 | ADC ");

  Serial.print(
      rawAdc
  );

  Serial.print("/4095 | ");

  Serial.print(
      engineeringValue,
      decimals
  );

  Serial.print(" ");

  Serial.println(
      unit
  );
}


void printCircuitSwitchLine(
    const char *label,
    bool highState,
    const char *highMeaning,
    const char *lowMeaning)
{
  Serial.print(label);
  Serial.print(" : ");

  if (highState)
  {
    Serial.print("RIGHT | HIGH | ");
    Serial.println(highMeaning);
  }
  else
  {
    Serial.print("LEFT  | LOW  | ");
    Serial.println(lowMeaning);
  }
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
  // CIRCUIT INPUT VALUES
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ CIRCUIT INPUT VALUES ]"
  );

  Serial.println(
      "Pot scale shown as Wokwi position / ESP32 ADC"
  );

  Serial.println();

  Serial.print(
      "DHT22 Temperature   : "
  );
  Serial.print(
      shelterTemperature,
      2
  );
  Serial.println(" C");

  Serial.print(
      "DHT22 Humidity      : "
  );
  Serial.print(
      humidity,
      2
  );
  Serial.println(" %");

  Serial.print(
      "DS18B20 PA Temp     : "
  );
  Serial.print(
      paTemperature,
      2
  );
  Serial.println(" C");

  Serial.println();

  printCircuitPotLine(
      "DC Voltage Pot D34 ",
      dcVoltageRaw,
      dcBusVoltage,
      2,
      "V"
  );

  printCircuitPotLine(
      "DC Current Pot D35 ",
      dcCurrentRaw,
      dcBusCurrent,
      2,
      "A"
  );

  printCircuitPotLine(
      "Battery Pot D32    ",
      batteryVoltageRaw,
      batteryVoltage,
      2,
      "V"
  );

  Serial.println();

  printCircuitPotLine(
      "RF Forward Pot D33 ",
      rfForwardRaw,
      rfForwardPower,
      2,
      "W"
  );

  printCircuitPotLine(
      "RF Reflect Pot VP  ",
      rfReflectedRaw,
      rfReflectedPower,
      2,
      "W"
  );

  Serial.println();

  printCircuitPotLine(
      "Latency Pot D25    ",
      latencyRaw,
      latency,
      1,
      "ms"
  );

  printCircuitPotLine(
      "Loss Pot D26       ",
      packetLossRaw,
      packetLoss,
      1,
      "%"
  );

  printCircuitPotLine(
      "RSSI Pot D27       ",
      rssiRaw,
      rssi,
      1,
      "dBm"
  );

  printCircuitPotLine(
      "Traffic Pot VN     ",
      trafficLoadRaw,
      trafficLoad,
      1,
      "%"
  );

  Serial.println();

  printCircuitSwitchLine(
      "Physical Link D14  ",
      linkUp,
      "UP",
      "DOWN"
  );

  printCircuitSwitchLine(
      "Upstream D13       ",
      upstreamReachable,
      "REACHABLE",
      "UNREACHABLE"
  );

  printCircuitSwitchLine(
      "Grid RX2 / GPIO16  ",
      gridAvailable,
      "AVAILABLE",
      "FAILED"
  );

  printCircuitSwitchLine(
      "Generator TX2/D17  ",
      generatorRunning,
      "RUNNING",
      "STOPPED"
  );

  printCircuitSwitchLine(
      "Cooling Fan D18    ",
      fanOperational,
      "OPERATIONAL",
      "FAILED"
  );

  printCircuitSwitchLine(
      "Rectifier D19      ",
      rectifierNormal,
      "NORMAL",
      "FAULT"
  );

  printCircuitSwitchLine(
      "Radio D23          ",
      radioOperational,
      "OPERATIONAL",
      "FAULT"
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
  // THERMAL SIGNAL PROCESSING
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ THERMAL SIGNAL PROCESSING ]"
  );

  Serial.print(
      "Shelter Temp EMA    : "
  );
  Serial.print(
      shelterTemperatureEMA,
      2
  );
  Serial.println(" C");

  Serial.print(
      "Shelter Temp StdDev : "
  );
  Serial.print(
      shelterTemperatureStats.stdDev,
      3
  );
  Serial.println(" C");

  Serial.print(
      "Shelter Temp Trend  : "
  );
  Serial.print(
      shelterTemperatureTrendCPerMin,
      3
  );
  Serial.println(" C/min");

  Serial.print(
      "Humidity EMA        : "
  );
  Serial.print(
      humidityEMA,
      2
  );
  Serial.println(" %");

  Serial.print(
      "Humidity Std Dev    : "
  );
  Serial.print(
      humidityStats.stdDev,
      3
  );
  Serial.println(" %");

  Serial.print(
      "PA Temp EMA         : "
  );
  Serial.print(
      paTemperatureEMA,
      2
  );
  Serial.println(" C");

  Serial.print(
      "PA Temp Std Dev     : "
  );
  Serial.print(
      paTemperatureStats.stdDev,
      3
  );
  Serial.println(" C");

  Serial.print(
      "PA Temp Trend       : "
  );
  Serial.print(
      paTemperatureTrendCPerMin,
      3
  );
  Serial.println(" C/min");

  Serial.print(
      "PA-Shelter Delta    : "
  );
  Serial.print(
      paShelterTemperatureDeltaC,
      2
  );
  Serial.println(" C");

  Serial.print(
      "Max Thermal Rise    : "
  );
  Serial.print(
      maximumThermalRiseCPerMin,
      3
  );
  Serial.println(" C/min");

  Serial.print(
      "Thermal Trend       : "
  );
  Serial.println(
      thermalTrendState
  );

  Serial.print(
      "Thermal Risk        : "
  );
  Serial.println(
      thermalRiskState
  );

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
  // POWER / BATTERY SIGNAL PROCESSING
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ POWER SIGNAL PROCESSING ]"
  );

  Serial.print(
      "DC Voltage EMA      : "
  );
  Serial.print(
      dcBusVoltageEMA,
      3
  );
  Serial.println(" V");

  Serial.print(
      "DC Voltage Std Dev  : "
  );
  Serial.print(
      dcVoltageStats.stdDev,
      4
  );
  Serial.println(" V");

  Serial.print(
      "DC Voltage Trend    : "
  );
  Serial.print(
      dcVoltageTrendVPerSec,
      4
  );
  Serial.println(" V/s");

  Serial.print(
      "DC Current EMA      : "
  );
  Serial.print(
      dcBusCurrentEMA,
      3
  );
  Serial.println(" A");

  Serial.print(
      "DC Current Std Dev  : "
  );
  Serial.print(
      dcCurrentStats.stdDev,
      4
  );
  Serial.println(" A");

  Serial.print(
      "DC Current Trend    : "
  );
  Serial.print(
      dcCurrentTrendAPerSec,
      4
  );
  Serial.println(" A/s");

  Serial.print(
      "DC Power EMA        : "
  );
  Serial.print(
      dcPowerEMA,
      2
  );
  Serial.println(" W");

  Serial.print(
      "DC Power Std Dev    : "
  );
  Serial.print(
      dcPowerStats.stdDev,
      3
  );
  Serial.println(" W");

  Serial.print(
      "DC Power Trend      : "
  );
  Serial.print(
      dcPowerTrendWPerSec,
      3
  );
  Serial.println(" W/s");

  Serial.print(
      "Battery Voltage EMA : "
  );
  Serial.print(
      batteryVoltageEMA,
      3
  );
  Serial.println(" V");

  Serial.print(
      "Battery Volt StdDev : "
  );
  Serial.print(
      batteryVoltageStats.stdDev,
      4
  );
  Serial.println(" V");

  Serial.print(
      "Battery Volt Trend  : "
  );
  Serial.print(
      batteryVoltageTrendVPerMin,
      4
  );
  Serial.println(" V/min");

  Serial.print(
      "Battery SoC EMA     : "
  );
  Serial.print(
      batterySOCEMA,
      2
  );
  Serial.println(" %");

  Serial.print(
      "Battery SoC StdDev  : "
  );
  Serial.print(
      batterySOCStats.stdDev,
      3
  );
  Serial.println(" %");

  Serial.print(
      "Battery SoC Trend   : "
  );
  Serial.print(
      batterySOCTrendPctPerMin,
      3
  );
  Serial.println(" %/min");

  Serial.print(
      "Battery Discharge   : "
  );
  Serial.print(
      batteryDischargeRatePctPerMin,
      3
  );
  Serial.println(" %/min");

  Serial.print(
      "Battery Trend State : "
  );
  Serial.println(
      batteryTrendState
  );

  Serial.print(
      "Statistics Window   : "
  );
  Serial.print(
      POWER_STATS_SAMPLES *
      FAST_INPUT_INTERVAL_MS /
      1000.0,
      2
  );
  Serial.println(" s");

  Serial.print(
      "EMA Alpha           : "
  );
  Serial.println(
      POWER_EMA_ALPHA,
      2
  );

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
  // RF SIGNAL PROCESSING
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ RF SIGNAL PROCESSING ]"
  );

  Serial.print(
      "Forward Power EMA   : "
  );
  Serial.print(
      rfForwardPowerEMA,
      3
  );
  Serial.println(" W");

  Serial.print(
      "Forward Std Dev     : "
  );
  Serial.print(
      rfForwardStats.stdDev,
      3
  );
  Serial.println(" W");

  Serial.print(
      "Forward Trend       : "
  );
  Serial.print(
      rfForwardTrendWPerSec,
      3
  );
  Serial.println(" W/s");

  Serial.print(
      "Reflected Power EMA : "
  );
  Serial.print(
      rfReflectedPowerEMA,
      3
  );
  Serial.println(" W");

  Serial.print(
      "Reflected Std Dev   : "
  );
  Serial.print(
      rfReflectedStats.stdDev,
      3
  );
  Serial.println(" W");

  Serial.print(
      "Reflected Trend     : "
  );
  Serial.print(
      rfReflectedTrendWPerSec,
      3
  );
  Serial.println(" W/s");

  Serial.print(
      "Reflection Ratio    : "
  );
  Serial.print(
      rfReflectionRatioPct,
      3
  );
  Serial.println(" %");

  Serial.print(
      "Reflection Ratio EMA: "
  );
  Serial.print(
      rfReflectionRatioPctEMA,
      3
  );
  Serial.println(" %");

  Serial.print(
      "Ratio Std Dev       : "
  );
  Serial.print(
      rfReflectionRatioStats.stdDev,
      3
  );
  Serial.println(" %");

  Serial.print(
      "Ratio Trend         : "
  );
  Serial.print(
      rfReflectionRatioTrendPctPerSec,
      3
  );
  Serial.println(" %/s");

  if (rfValid)
  {
    Serial.print(
        "VSWR EMA            : "
    );
    Serial.println(
        rfVswrEMA,
        3
    );

    Serial.print(
        "VSWR Trend          : "
    );
    Serial.print(
        rfVswrTrendPerSec,
        4
    );
    Serial.println(" /s");

    Serial.print(
        "Return Loss EMA     : "
    );
    Serial.print(
        rfReturnLossEMA,
        3
    );
    Serial.println(" dB");

    Serial.print(
        "Return Loss Trend   : "
    );
    Serial.print(
        rfReturnLossTrendDbPerSec,
        4
    );
    Serial.println(" dB/s");
  }
  else
  {
    Serial.println(
        "VSWR EMA            : INVALID"
    );

    Serial.println(
        "VSWR Trend          : INVALID"
    );

    Serial.println(
        "Return Loss EMA     : INVALID"
    );

    Serial.println(
        "Return Loss Trend   : INVALID"
    );
  }

  Serial.print(
      "RF Mismatch Event   : "
  );
  Serial.println(
      rfMismatchLatched
          ? "YES"
          : "NO"
  );

  Serial.print(
      "RF Statistics Window: "
  );
  Serial.print(
      POWER_STATS_SAMPLES *
      FAST_INPUT_INTERVAL_MS /
      1000.0f,
      2
  );
  Serial.println(" s");

  Serial.print(
      "RF EMA Alpha        : "
  );
  Serial.println(
      RF_SIGNAL_EMA_ALPHA,
      2
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
  // BACKHAUL SIGNAL PROCESSING
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ BACKHAUL SIGNAL PROCESSING ]"
  );

  Serial.print(
      "Latency EWMA        : "
  );
  Serial.print(
      latencyEMA,
      2
  );
  Serial.println(" ms");

  Serial.print(
      "Latency Jitter      : "
  );
  Serial.print(
      latencyJitterEWMA,
      3
  );
  Serial.println(" ms");

  Serial.print(
      "Latency Std Dev     : "
  );
  Serial.print(
      latencyStats.stdDev,
      3
  );
  Serial.println(" ms");

  Serial.print(
      "Latency Trend       : "
  );
  Serial.print(
      latencyTrendMsPerSec,
      3
  );
  Serial.println(" ms/s");

  Serial.print(
      "Latency Spike       : "
  );
  Serial.println(
      latencySpikeLatched
          ? "YES"
          : "NO"
  );

  Serial.print(
      "Packet Loss EWMA    : "
  );
  Serial.print(
      packetLossEMA,
      3
  );
  Serial.println(" %");

  Serial.print(
      "Packet Loss Std Dev : "
  );
  Serial.print(
      packetLossStats.stdDev,
      3
  );
  Serial.println(" %");

  Serial.print(
      "Packet Loss Trend   : "
  );
  Serial.print(
      packetLossTrendPctPerSec,
      3
  );
  Serial.println(" %/s");

  Serial.print(
      "Packet Loss Burst   : "
  );
  Serial.println(
      packetLossBurstLatched
          ? "YES"
          : "NO"
  );

  Serial.print(
      "RSSI EWMA           : "
  );
  Serial.print(
      rssiEMA,
      2
  );
  Serial.println(" dBm");

  Serial.print(
      "RSSI Slow Baseline  : "
  );
  Serial.print(
      rssiSlowBaselineEMA,
      2
  );
  Serial.println(" dBm");

  Serial.print(
      "RSSI Drop Magnitude : "
  );
  Serial.print(
      rssiDropMagnitudeDb,
      2
  );
  Serial.println(" dB");

  Serial.print(
      "RSSI Std Dev        : "
  );
  Serial.print(
      rssiStats.stdDev,
      3
  );
  Serial.println(" dB");

  Serial.print(
      "RSSI Trend          : "
  );
  Serial.print(
      rssiTrendDbPerSec,
      3
  );
  Serial.println(" dB/s");

  Serial.print(
      "RSSI Sudden Drop    : "
  );
  Serial.println(
      rssiDropLatched
          ? "YES"
          : "NO"
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
      "Stage               : DATA ACQUISITION + SIGNAL PROCESSING + ENERGY MODELLING"
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

  // --------------------------------------------------
  // TELEMETRY EVENT ACKNOWLEDGEMENT
  // --------------------------------------------------
  //
  // Events detected since the previous telemetry report
  // have now been shown to the user/dashboard pipeline.
  //
  // Clear the latches so the next report only contains
  // genuinely new transient events.
  latencySpikeLatched =
      false;

  packetLossBurstLatched =
      false;

  rssiDropLatched =
      false;

  rfMismatchLatched =
      false;
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



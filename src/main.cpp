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
const unsigned long LCD_PAGE_INTERVAL_MS = 3000;
const unsigned long MPU_INTERVAL_MS = 20;
const unsigned long DHT_INTERVAL_MS = 2000;

const unsigned long DS18B20_REQUEST_INTERVAL_MS = 2000;
const unsigned long DS18B20_CONVERSION_MS = 750;

const unsigned long TELEMETRY_INTERVAL_MS = 2000;

// AI recommendations normally arrive every ~5 seconds.
// If no fresh command arrives for 15 seconds, firmware
// automatically falls back to its local deterministic policy.
const unsigned long AI_COMMAND_TIMEOUT_MS = 15000;

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

// ====================================================
// ENERGY GUARDRAILS / HYSTERESIS / RECOVERY
// ====================================================
//
// These timings are demo-scale values.
//
// A production BTS would normally use longer confirmation
// periods based on operator policy and network requirements.
//

// Minimum time before a capacity-reducing mode transition.
//
// Capacity increases are allowed immediately.
const unsigned long MODE_MIN_DWELL_MS = 8000;

// After a major fault clears, all major-fault conditions
// must remain healthy continuously for this period before
// normal energy optimisation is allowed again.
const unsigned long RECOVERY_CONFIRMATION_MS = 6000;

// Traffic hysteresis:
//
// FULL:
//   enter >= 65%
//   leave only < 55%
//
// REDUCED:
//   enter < 15%
//   leave >= 25%
//
// This prevents operating-mode oscillation when traffic
// sits close to a single threshold.
const float TRAFFIC_FULL_ENTER_PCT = 65.0f;
const float TRAFFIC_FULL_EXIT_PCT = 55.0f;

const float TRAFFIC_REDUCED_ENTER_PCT = 15.0f;
const float TRAFFIC_REDUCED_EXIT_PCT = 25.0f;

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
// SINGLE ROTATING LCD
// ====================================================
//
// The original first LCD is retained at I2C address 0x20.
// It rotates through the values that were previously shown
// on the separate LCDs.
//
LiquidCrystal_I2C lcdDcVoltage(
    0x20,
    20,
    4
);

LiquidCrystal_I2C lcdAiRecommendations(
    0x21,
    20,
    4
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

// Requested mode is the safe target produced by the policy
// before minimum-dwell enforcement is applied.
String requestedOperatingMode = "UNKNOWN";
String requestedOperatingModeReason = "STARTING";

// ----------------------------------------------------
// AI RECOMMENDATION INPUT
// ----------------------------------------------------
//
// ABS_AI_CMD is produced by the laptop-side temporal AI.
//
// IMPORTANT:
// These values are recommendations only. The deterministic
// critical-energy, major-fault, traffic, degraded-service,
// recovery and dwell guardrails remain authoritative.
//
String aiRecommendedMode = "NONE";
String aiRecommendationReason = "NO AI COMMAND";
String aiFaultDomain = "UNKNOWN";

float aiDomainConfidence = 0.0f;
float aiAnomalyScore = 0.0f;

bool aiAnomalyFlag = false;
bool aiCommandEverReceived = false;

unsigned long lastAiCommandTime = 0;

String aiCommandStatus = "NOT_RECEIVED";
String aiSerialBuffer = "";

// Guardrail / recovery explanation state.
String guardrailStatus = "STARTING";
String recoveryState = "STABLE";

// True after a major fault has occurred and remains true
// until the healthy recovery-confirmation period completes.
bool recoveryActive = false;

unsigned long recoveryHealthySince = 0;
unsigned long lastOperatingModeChangeTime = 0;

unsigned long recoveryConfirmationRemainingMs = 0;
unsigned long modeDwellRemainingMs = 0;

// Useful stability KPI:
// how many actual operating-mode changes occurred.
unsigned long operatingModeChangeCount = 0;

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
// SINGLE LCD PAGE STATE
// ====================================================

uint8_t lcdPage = 0;
unsigned long lastLCDPageTime = 0;

String lastLCDLine0 = "";
String lastLCDLine1 = "";
String lastLCDLine2 = "";
String lastLCDLine3 = "";

String lastAiLCDLine0 = "";
String lastAiLCDLine1 = "";
String lastAiLCDLine2 = "";
String lastAiLCDLine3 = "";

// ====================================================
// LCD HELPERS
// ====================================================

String padLCDText(
    String text)
{
  if (text.length() > 20)
  {
    text =
        text.substring(
            0,
            20
        );
  }

  while (text.length() < 20)
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
// AI COMMAND PROTOCOL
// ====================================================
//
// Laptop -> ESP32:
//
// ABS_AI_CMD|{
//   "schema":"abs.ai.cmd.v1",
//   "recommended_mode":"ECO",
//   "reason":"...",
//   "fault_domain":"NORMAL",
//   "domain_confidence":0.91,
//   "anomaly_flag":false,
//   "anomaly_score":0.08
// }
//
// Firmware always validates the command and then applies
// deterministic safety guardrails before changing mode.
// ====================================================

bool extractAiStringField(
    const String &json,
    const char *key,
    String &value)
{
  String marker =
      "\"" +
      String(key) +
      "\":\"";

  int start =
      json.indexOf(marker);

  if (start < 0)
  {
    return false;
  }

  start +=
      marker.length();

  int end =
      json.indexOf(
          '"',
          start
      );

  if (end < 0)
  {
    return false;
  }

  value =
      json.substring(
          start,
          end
      );

  return true;
}


bool extractAiFloatField(
    const String &json,
    const char *key,
    float &value)
{
  String marker =
      "\"" +
      String(key) +
      "\":";

  int start =
      json.indexOf(marker);

  if (start < 0)
  {
    return false;
  }

  start +=
      marker.length();

  int end =
      start;

  while (end < json.length())
  {
    char c =
        json.charAt(end);

    bool numeric =
        (
            c >= '0' &&
            c <= '9'
        ) ||
        c == '-' ||
        c == '+' ||
        c == '.' ||
        c == 'e' ||
        c == 'E';

    if (!numeric)
    {
      break;
    }

    end++;
  }

  if (end <= start)
  {
    return false;
  }

  value =
      json.substring(
          start,
          end
      ).toFloat();

  return true;
}


bool extractAiBoolField(
    const String &json,
    const char *key,
    bool &value)
{
  String marker =
      "\"" +
      String(key) +
      "\":";

  int start =
      json.indexOf(marker);

  if (start < 0)
  {
    return false;
  }

  start +=
      marker.length();

  if (
      json.substring(
          start,
          start + 4
      ) == "true")
  {
    value = true;
    return true;
  }

  if (
      json.substring(
          start,
          start + 5
      ) == "false")
  {
    value = false;
    return true;
  }

  return false;
}


bool isValidAiMode(
    const String &mode)
{
  return
      (
          mode == "FULL" ||
          mode == "ECO" ||
          mode == "REDUCED" ||
          mode == "EMERGENCY"
      );
}


unsigned long getAiCommandAgeMs()
{
  if (!aiCommandEverReceived)
  {
    return 0;
  }

  return
      millis() -
      lastAiCommandTime;
}


bool isAiCommandFresh()
{
  if (!aiCommandEverReceived)
  {
    aiCommandStatus =
        "NOT_RECEIVED";

    return false;
  }

  if (
      getAiCommandAgeMs() >
      AI_COMMAND_TIMEOUT_MS)
  {
    aiCommandStatus =
        "STALE";

    return false;
  }

  aiCommandStatus =
      "FRESH";

  return true;
}


void printAiAck(
    bool accepted,
    const String &reason)
{
  Serial.print(
      "ABS_AI_ACK|{\"schema\":\"abs.ai.ack.v1\","
      "\"accepted\":"
  );

  Serial.print(
      accepted
          ? "true"
          : "false"
  );

  Serial.print(
      ",\"reason\":\""
  );

  Serial.print(reason);

  Serial.print(
      "\",\"recommended_mode\":\""
  );

  Serial.print(
      aiRecommendedMode
  );

  Serial.println(
      "\"}"
  );
}


void handleAiCommandLine(
    String line)
{
  line.trim();

  if (
      !line.startsWith(
          "ABS_AI_CMD|"
      ))
  {
    return;
  }

  String json =
      line.substring(
          11
      );

  String schema;
  String recommendedMode;
  String reason;
  String faultDomain;

  float domainConfidence = 0.0f;
  float anomalyScore = 0.0f;

  bool anomalyFlag = false;

  bool valid =
      extractAiStringField(
          json,
          "schema",
          schema
      ) &&
      extractAiStringField(
          json,
          "recommended_mode",
          recommendedMode
      ) &&
      extractAiStringField(
          json,
          "reason",
          reason
      ) &&
      extractAiStringField(
          json,
          "fault_domain",
          faultDomain
      ) &&
      extractAiFloatField(
          json,
          "domain_confidence",
          domainConfidence
      ) &&
      extractAiBoolField(
          json,
          "anomaly_flag",
          anomalyFlag
      ) &&
      extractAiFloatField(
          json,
          "anomaly_score",
          anomalyScore
      );

  if (!valid)
  {
    printAiAck(
        false,
        "MISSING_OR_INVALID_FIELD"
    );

    return;
  }

  if (
      schema !=
      "abs.ai.cmd.v1")
  {
    printAiAck(
        false,
        "UNSUPPORTED_SCHEMA"
    );

    return;
  }

  if (
      !isValidAiMode(
          recommendedMode
      ))
  {
    printAiAck(
        false,
        "INVALID_MODE"
    );

    return;
  }

  if (
      domainConfidence < 0.0f ||
      domainConfidence > 1.0f ||
      anomalyScore < 0.0f)
  {
    printAiAck(
        false,
        "INVALID_AI_NUMERIC_RANGE"
    );

    return;
  }

  aiRecommendedMode =
      recommendedMode;

  aiRecommendationReason =
      reason;

  aiFaultDomain =
      faultDomain;

  aiDomainConfidence =
      domainConfidence;

  aiAnomalyFlag =
      anomalyFlag;

  aiAnomalyScore =
      anomalyScore;

  lastAiCommandTime =
      millis();

  aiCommandEverReceived =
      true;

  aiCommandStatus =
      "FRESH";

  printAiAck(
      true,
      "ACCEPTED"
  );
}


void serviceAiCommandSerial()
{
  while (
      Serial.available() >
      0)
  {
    char c =
        Serial.read();

    if (c == '\r')
    {
      continue;
    }

    if (c == '\n')
    {
      if (
          aiSerialBuffer.length() >
          0)
      {
        handleAiCommandLine(
            aiSerialBuffer
        );

        aiSerialBuffer =
            "";
      }

      continue;
    }

    if (
        aiSerialBuffer.length() <
        768)
    {
      aiSerialBuffer +=
          c;
    }
    else
    {
      aiSerialBuffer =
          "";

      printAiAck(
          false,
          "COMMAND_TOO_LONG"
      );
    }
  }
}


// ====================================================
// GUARDED OPERATING-MODE CONTROLLER
// ====================================================


// ----------------------------------------------------
// MAJOR-FAULT GUARDRAIL
// ----------------------------------------------------
//
// These conditions require maximum service / recovery
// priority.
//
// Grid loss is deliberately NOT included here because a
// healthy site may legitimately continue in a battery-
// conservation mode after mains failure.
//
bool isMajorFaultActive()
{
  return
      (
          rfHealth == "FAULT" ||
          rfHealth == "SEVERE FAULT" ||

          electricalHealth == "FAULT" ||

          backhaulCondition == "CRITICAL" ||
          backhaulCondition == "OUTAGE" ||

          paTemperature >=
              PA_THERMAL_CRITICAL_C ||

          dynamicVibration >=
              3.0f ||

          !fanOperational ||
          !rectifierNormal ||
          !radioOperational
      );
}


// ----------------------------------------------------
// DEGRADED-SERVICE GUARDRAIL
// ----------------------------------------------------
//
// Degraded conditions do not necessarily require FULL
// capacity, but aggressive REDUCED operation is blocked.
//
bool isDegradedGuardrailActive()
{
  return
      (
          localSiteStatus == "DEGRADED" ||

          backhaulCondition == "DEGRADED" ||

          thermalRiskState == "FAST_RISE"
      );
}


// ----------------------------------------------------
// MODE CAPACITY RANK
// ----------------------------------------------------
//
// Used only to decide whether a transition is a service-
// capacity increase.
//
// Higher rank = greater available service capacity.
//
int operatingModeCapacityRank(
    const String &mode)
{
  if (mode == "FULL")
  {
    return 3;
  }

  if (mode == "ECO")
  {
    return 2;
  }

  if (mode == "REDUCED")
  {
    return 1;
  }

  // EMERGENCY intentionally represents minimum essential
  // operation.
  if (mode == "EMERGENCY")
  {
    return 0;
  }

  return -1;
}


// ----------------------------------------------------
// REQUESTED MODE
// ----------------------------------------------------
//
// Produces the safe requested mode.
//
// The final applied mode may remain higher temporarily due
// to minimum dwell time or recovery confirmation.
//
String determineRequestedOperatingMode()
{
  guardrailStatus =
      "PASSED";

  // --------------------------------------------------
  // CRITICAL BACKUP ENERGY
  // --------------------------------------------------
  //
  // Always authoritative. AI cannot override this.
  //
  if (
      !gridAvailable &&
      !generatorRunning &&
      batterySOC <= 25.0f)
  {
    requestedOperatingModeReason =
        "CRITICAL BACKUP ENERGY";

    guardrailStatus =
        "CRITICAL ENERGY PROTECTION";

    return "EMERGENCY";
  }

  // --------------------------------------------------
  // MAJOR FAULT
  // --------------------------------------------------
  //
  // Always authoritative. Maximum service/recovery capacity
  // takes priority over an AI energy-saving recommendation.
  //
  if (isMajorFaultActive())
  {
    requestedOperatingModeReason =
        "FAULT / RECOVERY PRIORITY";

    guardrailStatus =
        "FORCED FULL - MAJOR FAULT";

    return "FULL";
  }

  // --------------------------------------------------
  // BATTERY CONSERVATION
  // --------------------------------------------------
  //
  // Existing deterministic fallback remains authoritative.
  //
  if (
      !gridAvailable &&
      !generatorRunning)
  {
    requestedOperatingModeReason =
        "BATTERY CONSERVATION";

    return "ECO";
  }

  String candidateMode =
      "ECO";

  String candidateReason =
      "MODERATE TRAFFIC";

  bool aiFresh =
      isAiCommandFresh();

  // --------------------------------------------------
  // AI PRE-GUARDRAIL RECOMMENDATION
  // --------------------------------------------------

  if (aiFresh)
  {
    // EMERGENCY is a safety state and may only be entered
    // after the local critical-energy rule above independently
    // confirms it. A non-critical AI EMERGENCY request is
    // therefore clamped to ECO.
    if (
        aiRecommendedMode ==
        "EMERGENCY")
    {
      candidateMode =
          "ECO";

      candidateReason =
          "AI EMERGENCY CLAMPED TO ECO";

      guardrailStatus =
          "AI EMERGENCY BLOCKED - LOCAL SAFETY CHECK";
    }
    else
    {
      candidateMode =
          aiRecommendedMode;

      candidateReason =
          "AI: " +
          aiRecommendationReason;
    }
  }

  // --------------------------------------------------
  // LOCAL FALLBACK POLICY
  // --------------------------------------------------
  //
  // If AI is absent/stale, the previous deterministic
  // traffic policy remains fully operational.
  //
  else
  {
    if (
        trafficLoad >=
        TRAFFIC_FULL_ENTER_PCT)
    {
      candidateMode =
          "FULL";

      candidateReason =
          "HIGH TRAFFIC";
    }

    else if (
        operatingMode == "FULL" &&
        trafficLoad >=
            TRAFFIC_FULL_EXIT_PCT)
    {
      candidateMode =
          "FULL";

      candidateReason =
          "FULL TRAFFIC HYSTERESIS";
    }

    else if (
        operatingMode == "REDUCED")
    {
      if (
          trafficLoad >=
          TRAFFIC_REDUCED_EXIT_PCT)
      {
        candidateMode =
            "ECO";

        candidateReason =
            "TRAFFIC RECOVERY";
      }
      else
      {
        candidateMode =
            "REDUCED";

        candidateReason =
            "LOW TRAFFIC HYSTERESIS";
      }
    }

    else if (
        trafficLoad <
        TRAFFIC_REDUCED_ENTER_PCT)
    {
      candidateMode =
          "REDUCED";

      candidateReason =
          "LOW TRAFFIC";
    }

    else
    {
      candidateMode =
          "ECO";

      candidateReason =
          "MODERATE TRAFFIC";
    }
  }

  // --------------------------------------------------
  // HIGH-TRAFFIC CAPACITY GUARDRAIL
  // --------------------------------------------------
  //
  // This guardrail is applied AFTER the AI recommendation.
  // AI is not allowed to reduce service capacity while the
  // local site reports high traffic.
  //
  if (
      trafficLoad >=
      TRAFFIC_FULL_ENTER_PCT &&
      candidateMode !=
          "FULL")
  {
    candidateMode =
        "FULL";

    candidateReason =
        "HIGH TRAFFIC GUARDRAIL";

    guardrailStatus =
        "FORCED FULL - HIGH TRAFFIC";
  }

  else if (
      operatingMode == "FULL" &&
      trafficLoad >=
          TRAFFIC_FULL_EXIT_PCT &&
      candidateMode !=
          "FULL")
  {
    candidateMode =
        "FULL";

    candidateReason =
        "FULL TRAFFIC HYSTERESIS";

    guardrailStatus =
        "FULL HOLD - TRAFFIC HYSTERESIS";
  }

  // --------------------------------------------------
  // DEGRADED-SERVICE MINIMUM MODE
  // --------------------------------------------------

  if (
      isDegradedGuardrailActive() &&
      candidateMode == "REDUCED")
  {
    candidateMode =
        "ECO";

    candidateReason =
        "DEGRADED SERVICE GUARDRAIL";

    guardrailStatus =
        "MINIMUM ECO - DEGRADED SERVICE";
  }

  requestedOperatingModeReason =
      candidateReason;

  return candidateMode;
}


// ----------------------------------------------------
// OPERATING-MODE CONTROLLER
// ----------------------------------------------------

void updateOperatingModeController(
    unsigned long now)
{
  requestedOperatingMode =
      determineRequestedOperatingMode();

  bool majorFaultActive =
      isMajorFaultActive();

  // --------------------------------------------------
  // FAULT / RECOVERY STATE MACHINE
  // --------------------------------------------------

  if (majorFaultActive)
  {
    recoveryActive =
        true;

    recoveryHealthySince =
        0;

    recoveryConfirmationRemainingMs =
        RECOVERY_CONFIRMATION_MS;

    recoveryState =
        "FAULT_ACTIVE";
  }

  else if (
      recoveryActive &&
      requestedOperatingMode !=
          "EMERGENCY")
  {
    if (
        recoveryHealthySince ==
        0)
    {
      recoveryHealthySince =
          now;
    }

    unsigned long healthyElapsed =
        now -
        recoveryHealthySince;

    if (
        healthyElapsed <
        RECOVERY_CONFIRMATION_MS)
    {
      recoveryConfirmationRemainingMs =
          RECOVERY_CONFIRMATION_MS -
          healthyElapsed;

      recoveryState =
          "VERIFYING";

      requestedOperatingMode =
          "FULL";

      requestedOperatingModeReason =
          "RECOVERY CONFIRMATION";

      guardrailStatus =
          "RECOVERY CONFIRMATION HOLD";
    }
    else
    {
      recoveryActive =
          false;

      recoveryHealthySince =
          0;

      recoveryConfirmationRemainingMs =
          0;

      recoveryState =
          "STABLE";
    }
  }

  else
  {
    recoveryConfirmationRemainingMs =
        0;

    if (!recoveryActive)
    {
      recoveryState =
          "STABLE";
    }
  }

  // --------------------------------------------------
  // INITIAL MODE
  // --------------------------------------------------

  if (operatingMode == "UNKNOWN")
  {
    operatingMode =
        requestedOperatingMode;

    operatingModeReason =
        requestedOperatingModeReason;

    lastOperatingModeChangeTime =
        now;

    modeDwellRemainingMs =
        MODE_MIN_DWELL_MS;

    return;
  }

  // --------------------------------------------------
  // NO CHANGE REQUESTED
  // --------------------------------------------------

  if (
      requestedOperatingMode ==
      operatingMode)
  {
    operatingModeReason =
        requestedOperatingModeReason;

    unsigned long modeAge =
        now -
        lastOperatingModeChangeTime;

    if (
        modeAge <
        MODE_MIN_DWELL_MS)
    {
      modeDwellRemainingMs =
          MODE_MIN_DWELL_MS -
          modeAge;
    }
    else
    {
      modeDwellRemainingMs =
          0;
    }

    return;
  }

  // --------------------------------------------------
  // IMMEDIATE SAFETY / CAPACITY TRANSITIONS
  // --------------------------------------------------

  bool criticalEnergyTransition =
      (
          requestedOperatingMode ==
          "EMERGENCY"
      );

  bool capacityIncrease =
      (
          operatingModeCapacityRank(
              requestedOperatingMode
          ) >
          operatingModeCapacityRank(
              operatingMode
          )
      );

  if (
      criticalEnergyTransition ||
      capacityIncrease)
  {
    operatingMode =
        requestedOperatingMode;

    operatingModeReason =
        requestedOperatingModeReason;

    lastOperatingModeChangeTime =
        now;

    operatingModeChangeCount++;

    modeDwellRemainingMs =
        MODE_MIN_DWELL_MS;

    return;
  }

  // --------------------------------------------------
  // CAPACITY-REDUCING TRANSITION
  // --------------------------------------------------
  //
  // Reductions are only allowed after the current mode has
  // satisfied the minimum dwell period.
  //

  unsigned long modeAge =
      now -
      lastOperatingModeChangeTime;

  if (
      modeAge >=
      MODE_MIN_DWELL_MS)
  {
    operatingMode =
        requestedOperatingMode;

    operatingModeReason =
        requestedOperatingModeReason;

    lastOperatingModeChangeTime =
        now;

    operatingModeChangeCount++;

    modeDwellRemainingMs =
        MODE_MIN_DWELL_MS;
  }
  else
  {
    modeDwellRemainingMs =
        MODE_MIN_DWELL_MS -
        modeAge;

    operatingModeReason =
        "DWELL / HYSTERESIS HOLD";

    guardrailStatus =
        "DWELL HOLD - SAVING DELAYED";
  }
}


float getOperatingModePowerFactor(
    const String &mode)
{
  // Simulation assumptions for the baseline digital twin.
  //
  // FULL      = 100%
  // ECO       = 82%
  // REDUCED   = 65%
  // EMERGENCY = 50%

  if (mode == "ECO")
  {
    return 0.82f;
  }

  if (mode == "REDUCED")
  {
    return 0.65f;
  }

  if (mode == "EMERGENCY")
  {
    return 0.50f;
  }

  return 1.00f;
}


void updateEnergyModeMetrics()
{
  unsigned long now =
      millis();

  updateOperatingModeController(
      now
  );

  // Current DC power remains the first simulated
  // site-power baseline/proxy.
  baselineSitePowerKW =
      dcPower /
      1000.0f;

  float modeFactor =
      getOperatingModePowerFactor(
          operatingMode
      );

  managedSitePowerKW =
      baselineSitePowerKW *
      modeFactor;

  if (
      baselineSitePowerKW >
      0.0001f)
  {
    estimatedEnergySavingPct =
        (
            1.0f -
            modeFactor
        ) *
        100.0f;
  }
  else
  {
    estimatedEnergySavingPct =
        0.0f;
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
  // --------------------------------------------------
  // LCD 1 - LIVE VALUES
  // --------------------------------------------------

  lcdDcVoltage.init();
  lcdDcVoltage.backlight();
  lcdDcVoltage.clear();

  writeLCDLine(
      lcdDcVoltage,
      0,
      "LIVE VALUES"
  );

  writeLCDLine(
      lcdDcVoltage,
      1,
      "20x4 STATUS DISPLAY"
  );

  writeLCDLine(
      lcdDcVoltage,
      2,
      "Sensors / Network"
  );

  writeLCDLine(
      lcdDcVoltage,
      3,
      "Starting..."
  );

  // --------------------------------------------------
  // LCD 2 - AI RECOMMENDATIONS
  // --------------------------------------------------

  lcdAiRecommendations.init();
  lcdAiRecommendations.backlight();
  lcdAiRecommendations.clear();

  writeLCDLine(
      lcdAiRecommendations,
      0,
      "AI RECOMMENDATIONS"
  );

  writeLCDLine(
      lcdAiRecommendations,
      1,
      "Waiting for AI..."
  );

  writeLCDLine(
      lcdAiRecommendations,
      2,
      "FINAL:STARTING"
  );

  writeLCDLine(
      lcdAiRecommendations,
      3,
      "GUARD:STARTING"
  );

  lastLCDPageTime =
      millis();
}


// ====================================================
// DUAL 20x4 LCD REFRESH
// ====================================================
//
// LCD 1 rotates through live sensor/site/network values.
// LCD 2 always shows the latest AI diagnosis,
// recommendation, final applied mode and guardrail result.
//
void refreshLCDs(
    bool force = false)
{
  const uint8_t LCD_PAGE_COUNT = 3;

  unsigned long now =
      millis();

  if (
      now - lastLCDPageTime >=
      LCD_PAGE_INTERVAL_MS)
  {
    lcdPage =
        (
            lcdPage +
            1
        ) %
        LCD_PAGE_COUNT;

    lastLCDPageTime =
        now;

    force =
        true;
  }

  // ==================================================
  // LCD 1 - LIVE VALUES
  // ==================================================

  String line0;
  String line1;
  String line2;
  String line3;

  switch (lcdPage)
  {
    // --------------------------------------------------
    // PAGE 1 - MAIN ELECTRICAL / THERMAL VALUES
    // --------------------------------------------------
    case 0:
      line0 =
          "V" +
          String(
              dcBusVoltage,
              1
          ) +
          " I" +
          String(
              dcBusCurrent,
              1
          ) +
          " P" +
          String(
              dcPower,
              0
          ) +
          "W";

      line1 =
          "PA" +
          String(
              paTemperature,
              1
          ) +
          "C BAT" +
          String(
              batterySOC,
              0
          ) +
          "% H" +
          String(
              humidity,
              0
          ) +
          "%";

      line2 =
          "FAULT:" +
          faultCandidate;

      line3 =
          "MODE:" +
          operatingMode +
          " SAVE:" +
          String(
              estimatedEnergySavingPct,
              0
          ) +
          "%";
      break;

    // --------------------------------------------------
    // PAGE 2 - RF / BACKHAUL VALUES
    // --------------------------------------------------
    case 1:
      line0 =
          "RF F" +
          String(
              rfForwardPower,
              1
          ) +
          " R" +
          String(
              rfReflectedPower,
              1
          ) +
          "W";

      line1 =
          "VSWR" +
          String(
              vswr,
              2
          ) +
          " RF:" +
          rfHealth;

      line2 =
          "LAT" +
          String(
              latency,
              0
          ) +
          " L" +
          String(
              packetLoss,
              1
          ) +
          " R" +
          String(
              rssi,
              0
          );

      line3 =
          String("BH:") +
          backhaulCondition +
          " LINK:" +
          (
              linkUp
                  ? "UP"
                  : "DOWN"
          );
      break;

    // --------------------------------------------------
    // PAGE 3 - POWER SOURCE / EQUIPMENT VALUES
    // --------------------------------------------------
    case 2:
    default:
      line0 =
          String("GRID:") +
          (
              gridAvailable
                  ? "Y"
                  : "N"
          ) +
          " GEN:" +
          (
              generatorRunning
                  ? "ON"
                  : "OFF"
          ) +
          " SRC:" +
          activePowerSource;

      line1 =
          String("FAN:") +
          (
              fanOperational
                  ? "OK"
                  : "FAIL"
          ) +
          " RECT:" +
          (
              rectifierNormal
                  ? "OK"
                  : "FAIL"
          );

      line2 =
          String("RADIO:") +
          (
              radioOperational
                  ? "OK"
                  : "FAIL"
          ) +
          " LOCAL:" +
          localSiteStatus;

      line3 =
          "TRAFFIC:" +
          String(
              trafficLoad,
              1
          ) +
          "%";
      break;
  }

  String padded0 =
      padLCDText(
          line0
      );

  String padded1 =
      padLCDText(
          line1
      );

  String padded2 =
      padLCDText(
          line2
      );

  String padded3 =
      padLCDText(
          line3
      );

  if (
      force ||
      padded0 !=
          lastLCDLine0)
  {
    writeLCDLine(
        lcdDcVoltage,
        0,
        line0
    );

    lastLCDLine0 =
        padded0;
  }

  if (
      force ||
      padded1 !=
          lastLCDLine1)
  {
    writeLCDLine(
        lcdDcVoltage,
        1,
        line1
    );

    lastLCDLine1 =
        padded1;
  }

  if (
      force ||
      padded2 !=
          lastLCDLine2)
  {
    writeLCDLine(
        lcdDcVoltage,
        2,
        line2
    );

    lastLCDLine2 =
        padded2;
  }

  if (
      force ||
      padded3 !=
          lastLCDLine3)
  {
    writeLCDLine(
        lcdDcVoltage,
        3,
        line3
    );

    lastLCDLine3 =
        padded3;
  }

  // ==================================================
  // LCD 2 - AI RECOMMENDATIONS
  // ==================================================

  String aiLine0;
  String aiLine1;
  String aiLine2;
  String aiLine3;

  if (aiCommandEverReceived)
  {
    aiLine0 =
        "AI:" +
        aiFaultDomain +
        " " +
        String(
            aiDomainConfidence *
            100.0f,
            1
        ) +
        "%";

    aiLine1 =
        "REC:" +
        aiRecommendedMode +
        " AN:" +
        (
            aiAnomalyFlag
                ? "YES"
                : "NO"
        );

    aiLine2 =
        "FINAL:" +
        operatingMode +
        " REQ:" +
        requestedOperatingMode;

    aiLine3 =
        "GUARD:" +
        guardrailStatus;
  }
  else
  {
    aiLine0 =
        "AI:WAITING";

    aiLine1 =
        "REC:NONE";

    aiLine2 =
        "FINAL:" +
        operatingMode;

    aiLine3 =
        "GUARD:" +
        guardrailStatus;
  }

  String aiPadded0 =
      padLCDText(
          aiLine0
      );

  String aiPadded1 =
      padLCDText(
          aiLine1
      );

  String aiPadded2 =
      padLCDText(
          aiLine2
      );

  String aiPadded3 =
      padLCDText(
          aiLine3
      );

  if (
      force ||
      aiPadded0 !=
          lastAiLCDLine0)
  {
    writeLCDLine(
        lcdAiRecommendations,
        0,
        aiLine0
    );

    lastAiLCDLine0 =
        aiPadded0;
  }

  if (
      force ||
      aiPadded1 !=
          lastAiLCDLine1)
  {
    writeLCDLine(
        lcdAiRecommendations,
        1,
        aiLine1
    );

    lastAiLCDLine1 =
        aiPadded1;
  }

  if (
      force ||
      aiPadded2 !=
          lastAiLCDLine2)
  {
    writeLCDLine(
        lcdAiRecommendations,
        2,
        aiLine2
    );

    lastAiLCDLine2 =
        aiPadded2;
  }

  if (
      force ||
      aiPadded3 !=
          lastAiLCDLine3)
  {
    writeLCDLine(
        lcdAiRecommendations,
        3,
        aiLine3
    );

    lastAiLCDLine3 =
        aiPadded3;
  }
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
// MACHINE-READABLE TELEMETRY
// ====================================================
//
// One complete JSON object is written on a single line.
//
// The ABS_JSON| prefix lets the future Pico / Python receiver
// distinguish structured telemetry from the human-readable
// diagnostic output.
//
// Human-readable telemetry remains unchanged.
//
void printMachineReadableTelemetry()
{
  Serial.print("ABS_JSON|{");

  // --------------------------------------------------
  // RECORD INFORMATION
  // --------------------------------------------------

  Serial.print("\"schema\":\"abs.v1\"");

  Serial.print(",\"timestamp_ms\":");
  Serial.print(millis());

  // --------------------------------------------------
  // ENVIRONMENT / THERMAL
  // --------------------------------------------------

  Serial.print(",\"shelter_temp_c\":");
  Serial.print(shelterTemperature, 2);

  Serial.print(",\"humidity_pct\":");
  Serial.print(humidity, 2);

  Serial.print(",\"pa_temp_c\":");
  Serial.print(paTemperature, 2);

  Serial.print(",\"pa_temp_ema_c\":");
  Serial.print(paTemperatureEMA, 2);

  Serial.print(",\"pa_temp_trend_c_per_min\":");
  Serial.print(paTemperatureTrendCPerMin, 3);

  Serial.print(",\"pa_shelter_delta_c\":");
  Serial.print(paShelterTemperatureDeltaC, 2);

  // --------------------------------------------------
  // VIBRATION FEATURES
  // --------------------------------------------------

  Serial.print(",\"dynamic_vibration_mps2\":");
  Serial.print(dynamicVibration, 4);

  Serial.print(",\"vibration_rms_mps2\":");
  Serial.print(vibrationRMS, 4);

  Serial.print(",\"vibration_std_mps2\":");
  Serial.print(vibrationStdDev, 4);

  Serial.print(",\"vibration_peak_to_peak_mps2\":");
  Serial.print(vibrationPeakToPeak, 4);

  Serial.print(",\"vibration_dominant_hz\":");
  Serial.print(dominantVibrationFrequencyHz, 3);

  Serial.print(",\"vibration_centroid_hz\":");
  Serial.print(vibrationSpectralCentroidHz, 3);

  Serial.print(",\"vibration_spectral_energy\":");
  Serial.print(vibrationSpectralEnergy, 6);

  // --------------------------------------------------
  // POWER / BATTERY
  // --------------------------------------------------

  Serial.print(",\"dc_voltage_v\":");
  Serial.print(dcBusVoltage, 3);

  Serial.print(",\"dc_current_a\":");
  Serial.print(dcBusCurrent, 3);

  Serial.print(",\"dc_power_w\":");
  Serial.print(dcPower, 2);

  Serial.print(",\"dc_voltage_ema_v\":");
  Serial.print(dcBusVoltageEMA, 3);

  Serial.print(",\"dc_current_ema_a\":");
  Serial.print(dcBusCurrentEMA, 3);

  Serial.print(",\"dc_power_ema_w\":");
  Serial.print(dcPowerEMA, 2);

  Serial.print(",\"dc_voltage_std_v\":");
  Serial.print(dcVoltageStats.stdDev, 4);

  Serial.print(",\"dc_current_std_a\":");
  Serial.print(dcCurrentStats.stdDev, 4);

  Serial.print(",\"dc_power_std_w\":");
  Serial.print(dcPowerStats.stdDev, 3);

  Serial.print(",\"dc_voltage_trend_vps\":");
  Serial.print(dcVoltageTrendVPerSec, 4);

  Serial.print(",\"dc_current_trend_aps\":");
  Serial.print(dcCurrentTrendAPerSec, 4);

  Serial.print(",\"dc_power_trend_wps\":");
  Serial.print(dcPowerTrendWPerSec, 3);

  Serial.print(",\"battery_voltage_v\":");
  Serial.print(batteryVoltage, 3);

  Serial.print(",\"battery_soc_pct\":");
  Serial.print(batterySOC, 2);

  Serial.print(",\"battery_voltage_ema_v\":");
  Serial.print(batteryVoltageEMA, 3);

  Serial.print(",\"battery_soc_ema_pct\":");
  Serial.print(batterySOCEMA, 2);

  Serial.print(",\"battery_soc_trend_pct_per_min\":");
  Serial.print(batterySOCTrendPctPerMin, 3);

  // --------------------------------------------------
  // RF
  // --------------------------------------------------

  Serial.print(",\"rf_forward_w\":");
  Serial.print(rfForwardPower, 3);

  Serial.print(",\"rf_reflected_w\":");
  Serial.print(rfReflectedPower, 3);

  Serial.print(",\"rf_forward_ema_w\":");
  Serial.print(rfForwardPowerEMA, 3);

  Serial.print(",\"rf_reflected_ema_w\":");
  Serial.print(rfReflectedPowerEMA, 3);

  Serial.print(",\"rf_reflection_ratio_pct\":");
  Serial.print(rfReflectionRatioPct, 3);

  Serial.print(",\"rf_reflection_ratio_ema_pct\":");
  Serial.print(rfReflectionRatioPctEMA, 3);

  Serial.print(",\"vswr\":");
  Serial.print(vswr, 3);

  Serial.print(",\"vswr_ema\":");
  Serial.print(rfVswrEMA, 3);

  Serial.print(",\"return_loss_db\":");
  Serial.print(returnLoss, 3);

  Serial.print(",\"return_loss_ema_db\":");
  Serial.print(rfReturnLossEMA, 3);

  // --------------------------------------------------
  // BACKHAUL
  // --------------------------------------------------

  Serial.print(",\"latency_ms\":");
  Serial.print(latency, 2);

  Serial.print(",\"latency_ema_ms\":");
  Serial.print(latencyEMA, 2);

  Serial.print(",\"latency_jitter_ms\":");
  Serial.print(latencyJitterEWMA, 3);

  Serial.print(",\"packet_loss_pct\":");
  Serial.print(packetLoss, 3);

  Serial.print(",\"packet_loss_ema_pct\":");
  Serial.print(packetLossEMA, 3);

  Serial.print(",\"rssi_dbm\":");
  Serial.print(rssi, 2);

  Serial.print(",\"rssi_ema_dbm\":");
  Serial.print(rssiEMA, 2);

  Serial.print(",\"rssi_slow_baseline_dbm\":");
  Serial.print(rssiSlowBaselineEMA, 2);

  Serial.print(",\"rssi_drop_db\":");
  Serial.print(rssiDropMagnitudeDb, 2);

  Serial.print(",\"traffic_load_pct\":");
  Serial.print(trafficLoad, 2);

  // --------------------------------------------------
  // DIGITAL INPUT STATES
  // --------------------------------------------------

  Serial.print(",\"physical_link_up\":");
  Serial.print(linkUp ? "true" : "false");

  Serial.print(",\"upstream_reachable\":");
  Serial.print(upstreamReachable ? "true" : "false");

  Serial.print(",\"grid_available\":");
  Serial.print(gridAvailable ? "true" : "false");

  Serial.print(",\"generator_running\":");
  Serial.print(generatorRunning ? "true" : "false");

  Serial.print(",\"fan_operational\":");
  Serial.print(fanOperational ? "true" : "false");

  Serial.print(",\"rectifier_normal\":");
  Serial.print(rectifierNormal ? "true" : "false");

  Serial.print(",\"radio_operational\":");
  Serial.print(radioOperational ? "true" : "false");

  // --------------------------------------------------
  // TRANSIENT EVENTS
  // --------------------------------------------------

  Serial.print(",\"latency_spike\":");
  Serial.print(latencySpikeLatched ? "true" : "false");

  Serial.print(",\"packet_loss_burst\":");
  Serial.print(packetLossBurstLatched ? "true" : "false");

  Serial.print(",\"rssi_sudden_drop\":");
  Serial.print(rssiDropLatched ? "true" : "false");

  Serial.print(",\"rf_mismatch_event\":");
  Serial.print(rfMismatchLatched ? "true" : "false");

  // --------------------------------------------------
  // RULE-BASED GROUND TRUTH / STATUS
  // --------------------------------------------------

  Serial.print(",\"rf_health\":\"");
  Serial.print(rfHealth);
  Serial.print("\"");

  Serial.print(",\"electrical_health\":\"");
  Serial.print(electricalHealth);
  Serial.print("\"");

  Serial.print(",\"thermal_risk\":\"");
  Serial.print(thermalRiskState);
  Serial.print("\"");

  Serial.print(",\"backhaul_status\":\"");
  Serial.print(backhaulCondition);
  Serial.print("\"");

  Serial.print(",\"local_site_status\":\"");
  Serial.print(localSiteStatus);
  Serial.print("\"");

  Serial.print(",\"fault_label\":\"");
  Serial.print(faultCandidate);
  Serial.print("\"");

  // --------------------------------------------------
  // AI COMMAND / DIAGNOSIS REFERENCE
  // --------------------------------------------------
  //
  // These are output/status fields only. They are explicitly
  // forbidden as future ML features.
  //
  isAiCommandFresh();

  Serial.print(",\"ai_command_status\":\"");
  Serial.print(aiCommandStatus);
  Serial.print("\"");

  Serial.print(",\"ai_recommended_mode\":\"");
  Serial.print(aiRecommendedMode);
  Serial.print("\"");

  Serial.print(",\"ai_recommendation_reason\":\"");
  Serial.print(aiRecommendationReason);
  Serial.print("\"");

  Serial.print(",\"ai_fault_domain\":\"");
  Serial.print(aiFaultDomain);
  Serial.print("\"");

  Serial.print(",\"ai_domain_confidence\":");
  Serial.print(aiDomainConfidence, 4);

  Serial.print(",\"ai_anomaly_flag\":");
  Serial.print(
      aiAnomalyFlag
          ? "true"
          : "false"
  );

  Serial.print(",\"ai_anomaly_score\":");
  Serial.print(aiAnomalyScore, 4);

  Serial.print(",\"ai_command_age_ms\":");

  if (aiCommandEverReceived)
  {
    Serial.print(
        getAiCommandAgeMs()
    );
  }
  else
  {
    Serial.print(-1);
  }

  // --------------------------------------------------
  // ENERGY / CONTROL
  // --------------------------------------------------

  Serial.print(",\"active_power_source\":\"");
  Serial.print(activePowerSource);
  Serial.print("\"");

  Serial.print(",\"energy_action\":\"");
  Serial.print(energyAction);
  Serial.print("\"");

  Serial.print(",\"requested_mode\":\"");
  Serial.print(requestedOperatingMode);
  Serial.print("\"");

  Serial.print(",\"operating_mode\":\"");
  Serial.print(operatingMode);
  Serial.print("\"");

  Serial.print(",\"guardrail_status\":\"");
  Serial.print(guardrailStatus);
  Serial.print("\"");

  Serial.print(",\"recovery_state\":\"");
  Serial.print(recoveryState);
  Serial.print("\"");

  Serial.print(",\"baseline_power_kw\":");
  Serial.print(baselineSitePowerKW, 4);

  Serial.print(",\"managed_power_kw\":");
  Serial.print(managedSitePowerKW, 4);

  Serial.print(",\"energy_saving_pct\":");
  Serial.print(estimatedEnergySavingPct, 2);

  Serial.print(",\"mode_change_count\":");
  Serial.print(operatingModeChangeCount);

  // --------------------------------------------------
  // RAW CIRCUIT ADC VALUES
  // --------------------------------------------------

  Serial.print(",\"raw_adc\":{");

  Serial.print("\"dc_voltage\":");
  Serial.print(dcVoltageRaw);

  Serial.print(",\"dc_current\":");
  Serial.print(dcCurrentRaw);

  Serial.print(",\"battery_voltage\":");
  Serial.print(batteryVoltageRaw);

  Serial.print(",\"rf_forward\":");
  Serial.print(rfForwardRaw);

  Serial.print(",\"rf_reflected\":");
  Serial.print(rfReflectedRaw);

  Serial.print(",\"latency\":");
  Serial.print(latencyRaw);

  Serial.print(",\"packet_loss\":");
  Serial.print(packetLossRaw);

  Serial.print(",\"rssi\":");
  Serial.print(rssiRaw);

  Serial.print(",\"traffic\":");
  Serial.print(trafficLoadRaw);

  Serial.print("}");

  Serial.println("}");
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

  Serial.print(
      "Mode Source         : "
  );
  Serial.println(
      isAiCommandFresh()
          ? "AI RECOMMENDATION + DETERMINISTIC GUARDRAILS"
          : "LOCAL FALLBACK + DETERMINISTIC GUARDRAILS"
  );

  // --------------------------------------------------
  // GUARDRAILS / HYSTERESIS / RECOVERY
  // --------------------------------------------------

  Serial.println();

  Serial.println(
      "[ GUARDRAILS / HYSTERESIS / RECOVERY ]"
  );

  Serial.print(
      "Requested Mode      : "
  );
  Serial.println(
      requestedOperatingMode
  );

  Serial.print(
      "Requested Reason    : "
  );
  Serial.println(
      requestedOperatingModeReason
  );

  Serial.print(
      "Applied Mode        : "
  );
  Serial.println(
      operatingMode
  );

  Serial.print(
      "Guardrail Status    : "
  );
  Serial.println(
      guardrailStatus
  );

  Serial.print(
      "Recovery State      : "
  );
  Serial.println(
      recoveryState
  );

  Serial.print(
      "Recovery Remaining  : "
  );
  Serial.print(
      recoveryConfirmationRemainingMs /
      1000.0f,
      1
  );
  Serial.println(" s");

  Serial.print(
      "Mode Dwell Remaining: "
  );
  Serial.print(
      modeDwellRemainingMs /
      1000.0f,
      1
  );
  Serial.println(" s");

  Serial.print(
      "Mode Changes        : "
  );
  Serial.println(
      operatingModeChangeCount
  );

  Serial.println(
      "FULL Hysteresis     : enter 65% / exit 55%"
  );

  Serial.println(
      "REDUCED Hysteresis  : enter 15% / exit 25%"
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
      "Stage               : SIGNAL PROCESSING + GUARDED CONTROL + STRUCTURED TELEMETRY"
  );

  Serial.print(
      "AI Command Status   : "
  );
  Serial.println(
      aiCommandStatus
  );

  Serial.print(
      "AI Recommended Mode : "
  );
  Serial.println(
      aiRecommendedMode
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
  // MACHINE-READABLE RECORD
  // --------------------------------------------------
  //
  // Print before clearing transient-event latches so the
  // structured record receives the same event information
  // as the human-readable report.
  //
  Serial.println();

  Serial.println(
      "[ MACHINE READABLE TELEMETRY ]"
  );

  printMachineReadableTelemetry();

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
  // AI COMMAND INPUT
  // ==================================================
  //
  // Non-blocking serial parser. Any accepted recommendation
  // is consumed by the next 50 ms guarded control refresh.
  //
  serviceAiCommandSerial();

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



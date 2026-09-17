#include <Arduino.h>

// ============================================================
// AUTONOMOUS BASE STATION
// RASPBERRY PI PICO - EMBEDDED AI NODE
//
// Stage 1:
//   Receive machine-readable telemetry
//   Validate ABS telemetry schema
//   Extract selected AI features
//   Produce structured acknowledgement
//
// AI model deployment comes later.
// ============================================================

static const unsigned long UART_BAUD = 115200;

static const char *TELEMETRY_PREFIX = "ABS_JSON|";
static const char *SUPPORTED_SCHEMA = "abs.v1";

String receiveBuffer;

// ============================================================
// DEMO TELEMETRY
// ============================================================
//
// Type:
//
//   DEMO
//
// into the Wokwi serial monitor to test the receiver without
// having to paste a complete ESP32 telemetry line.
//
const char *DEMO_PACKET =
    "ABS_JSON|{"
    "\"schema\":\"abs.v1\","
    "\"timestamp_ms\":32668,"
    "\"pa_temp_c\":42.00,"
    "\"battery_soc_pct\":75.84,"
    "\"rf_forward_w\":73.309,"
    "\"rf_reflected_w\":1.954,"
    "\"vswr\":1.390,"
    "\"latency_ms\":43.85,"
    "\"packet_loss_pct\":0.781,"
    "\"rssi_dbm\":-55.26,"
    "\"traffic_load_pct\":34.21,"
    "\"physical_link_up\":true,"
    "\"upstream_reachable\":true,"
    "\"grid_available\":true,"
    "\"fan_operational\":true,"
    "\"rectifier_normal\":true,"
    "\"radio_operational\":true,"
    "\"fault_label\":\"NORMAL\","
    "\"operating_mode\":\"ECO\","
    "\"guardrail_status\":\"PASSED\""
    "}";


// ============================================================
// JSON FIELD HELPERS
// ============================================================
//
// These lightweight functions deliberately avoid a JSON
// library for Receiver V1.
//
// The ESP32 telemetry format is under our control, so we can
// extract the small feature subset required by the Pico.
//
// A complete structured parser can be introduced later if the
// embedded model requires the entire record.
// ============================================================

bool extractStringField(
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
          start);

  if (end < 0)
  {
    return false;
  }

  value =
      json.substring(
          start,
          end);

  return true;
}


bool extractFloatField(
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
        (c >= '0' && c <= '9') ||
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
          end)
          .toFloat();

  return true;
}


bool extractBoolField(
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
          start + 4) == "true")
  {
    value = true;
    return true;
  }

  if (
      json.substring(
          start,
          start + 5) == "false")
  {
    value = false;
    return true;
  }

  return false;
}


// ============================================================
// RESPONSE HELPERS
// ============================================================

void printRejectedPacket(
    const String &reason)
{
  Serial1.println();
  Serial1.println(
      "[ PICO TELEMETRY REJECTED ]");

  Serial1.print(
      "Reason              : ");
  Serial1.println(reason);

  Serial1.print(
      "PICO_RESULT|{\"schema\":\"pico.v1\","
      "\"telemetry_ok\":false,"
      "\"reason\":\"");

  Serial1.print(reason);

  Serial1.println(
      "\"}");
}


// ============================================================
// TELEMETRY HANDLER
// ============================================================

void handleTelemetryLine(
    String line)
{
  line.trim();

  if (line.length() == 0)
  {
    return;
  }

  // ----------------------------------------------------------
  // BUILT-IN DEMO COMMAND
  // ----------------------------------------------------------

  if (line == "DEMO")
  {
    Serial1.println();
    Serial1.println(
        "[ DEMO PACKET INJECTED ]");

    line =
        String(DEMO_PACKET);
  }

  // ----------------------------------------------------------
  // CHECK TRANSPORT PREFIX
  // ----------------------------------------------------------

  if (!line.startsWith(TELEMETRY_PREFIX))
  {
    printRejectedPacket(
        "INVALID_PREFIX");

    return;
  }

  String json =
      line.substring(
          strlen(
              TELEMETRY_PREFIX));

  // ----------------------------------------------------------
  // REQUIRED FIELDS
  // ----------------------------------------------------------

  String schema;
  String sourceFaultLabel;
  String sourceOperatingMode;
  String guardrailStatus;

  float paTemperature = 0.0f;
  float batterySoc = 0.0f;
  float rfForward = 0.0f;
  float rfReflected = 0.0f;
  float vswr = 0.0f;
  float latency = 0.0f;
  float packetLoss = 0.0f;
  float rssi = 0.0f;
  float trafficLoad = 0.0f;

  bool physicalLinkUp = false;
  bool upstreamReachable = false;
  bool gridAvailable = false;
  bool fanOperational = false;
  bool rectifierNormal = false;
  bool radioOperational = false;

  bool valid =
      extractStringField(
          json,
          "schema",
          schema) &&

      extractFloatField(
          json,
          "pa_temp_c",
          paTemperature) &&

      extractFloatField(
          json,
          "battery_soc_pct",
          batterySoc) &&

      extractFloatField(
          json,
          "rf_forward_w",
          rfForward) &&

      extractFloatField(
          json,
          "rf_reflected_w",
          rfReflected) &&

      extractFloatField(
          json,
          "vswr",
          vswr) &&

      extractFloatField(
          json,
          "latency_ms",
          latency) &&

      extractFloatField(
          json,
          "packet_loss_pct",
          packetLoss) &&

      extractFloatField(
          json,
          "rssi_dbm",
          rssi) &&

      extractFloatField(
          json,
          "traffic_load_pct",
          trafficLoad) &&

      extractBoolField(
          json,
          "physical_link_up",
          physicalLinkUp) &&

      extractBoolField(
          json,
          "upstream_reachable",
          upstreamReachable) &&

      extractBoolField(
          json,
          "grid_available",
          gridAvailable) &&

      extractBoolField(
          json,
          "fan_operational",
          fanOperational) &&

      extractBoolField(
          json,
          "rectifier_normal",
          rectifierNormal) &&

      extractBoolField(
          json,
          "radio_operational",
          radioOperational) &&

      extractStringField(
          json,
          "fault_label",
          sourceFaultLabel) &&

      extractStringField(
          json,
          "operating_mode",
          sourceOperatingMode) &&

      extractStringField(
          json,
          "guardrail_status",
          guardrailStatus);

  if (!valid)
  {
    printRejectedPacket(
        "MISSING_REQUIRED_FIELD");

    return;
  }

  // ----------------------------------------------------------
  // SCHEMA CHECK
  // ----------------------------------------------------------

  if (schema != SUPPORTED_SCHEMA)
  {
    printRejectedPacket(
        "UNSUPPORTED_SCHEMA");

    return;
  }

  // Blink the built-in LED each time a valid telemetry packet
  // reaches the embedded AI node.
  digitalWrite(
      LED_BUILTIN,
      HIGH);

  // ----------------------------------------------------------
  // HUMAN-READABLE PICO REPORT
  // ----------------------------------------------------------

  Serial1.println();
  Serial1.println(
      "================================================");
  Serial1.println(
      "          RASPBERRY PI PICO AI NODE");
  Serial1.println(
      "================================================");

  Serial1.println();

  Serial1.println(
      "[ TELEMETRY RECEIVER ]");

  Serial1.println(
      "Telemetry Received  : YES");

  Serial1.print(
      "Schema              : ");
  Serial1.println(schema);

  Serial1.println();

  Serial1.println(
      "[ SELECTED AI FEATURES ]");

  Serial1.print(
      "PA Temperature      : ");
  Serial1.print(
      paTemperature,
      2);
  Serial1.println(
      " C");

  Serial1.print(
      "Battery SoC         : ");
  Serial1.print(
      batterySoc,
      2);
  Serial1.println(
      " %");

  Serial1.print(
      "RF Forward Power    : ");
  Serial1.print(
      rfForward,
      3);
  Serial1.println(
      " W");

  Serial1.print(
      "RF Reflected Power  : ");
  Serial1.print(
      rfReflected,
      3);
  Serial1.println(
      " W");

  Serial1.print(
      "VSWR                : ");
  Serial1.println(
      vswr,
      3);

  Serial1.print(
      "Latency             : ");
  Serial1.print(
      latency,
      2);
  Serial1.println(
      " ms");

  Serial1.print(
      "Packet Loss         : ");
  Serial1.print(
      packetLoss,
      3);
  Serial1.println(
      " %");

  Serial1.print(
      "RSSI                : ");
  Serial1.print(
      rssi,
      2);
  Serial1.println(
      " dBm");

  Serial1.print(
      "Traffic Load        : ");
  Serial1.print(
      trafficLoad,
      2);
  Serial1.println(
      " %");

  Serial1.println();

  Serial1.println(
      "[ SITE STATES ]");

  Serial1.print(
      "Physical Link       : ");
  Serial1.println(
      physicalLinkUp
          ? "UP"
          : "DOWN");

  Serial1.print(
      "Upstream Reachable  : ");
  Serial1.println(
      upstreamReachable
          ? "YES"
          : "NO");

  Serial1.print(
      "Grid Available      : ");
  Serial1.println(
      gridAvailable
          ? "YES"
          : "NO");

  Serial1.print(
      "Cooling Fan         : ");
  Serial1.println(
      fanOperational
          ? "OPERATIONAL"
          : "FAILED");

  Serial1.print(
      "Rectifier           : ");
  Serial1.println(
      rectifierNormal
          ? "NORMAL"
          : "FAULT");

  Serial1.print(
      "Radio               : ");
  Serial1.println(
      radioOperational
          ? "OPERATIONAL"
          : "FAULT");

  Serial1.println();

  Serial1.println(
      "[ CURRENT RULE-BASED REFERENCES ]");

  Serial1.print(
      "Ground Truth Fault  : ");
  Serial1.println(
      sourceFaultLabel);

  Serial1.print(
      "ESP32 Energy Mode   : ");
  Serial1.println(
      sourceOperatingMode);

  Serial1.print(
      "Guardrail Status    : ");
  Serial1.println(
      guardrailStatus);

  Serial1.println();

  Serial1.println(
      "[ EMBEDDED AI ]");

  Serial1.println(
      "AI Model            : NOT YET LOADED");

  Serial1.println(
      "AI Diagnosis        : NOT YET ACTIVE");

  Serial1.println(
      "Energy AI           : NOT YET ACTIVE");

  // ----------------------------------------------------------
  // MACHINE-READABLE PICO RESPONSE
  // ----------------------------------------------------------

  Serial1.print(
      "PICO_RESULT|{"
      "\"schema\":\"pico.v1\","
      "\"telemetry_ok\":true,"
      "\"source_schema\":\"");

  Serial1.print(
      schema);

  Serial1.print(
      "\",\"source_fault_label\":\"");

  Serial1.print(
      sourceFaultLabel);

  Serial1.print(
      "\",\"source_operating_mode\":\"");

  Serial1.print(
      sourceOperatingMode);

  Serial1.println(
      "\",\"ai_model\":\"NOT_LOADED\"}");

  digitalWrite(
      LED_BUILTIN,
      LOW);
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  pinMode(
      LED_BUILTIN,
      OUTPUT);

  digitalWrite(
      LED_BUILTIN,
      LOW);

  // Serial1 = Pico UART0
  // GP0 = TX
  // GP1 = RX
  //
  // In the Wokwi Pico project, the Serial Monitor is connected
  // to these pins so it acts as the telemetry source.
  Serial1.begin(
      UART_BAUD);

  delay(500);

  Serial1.println();
  Serial1.println(
      "================================================");
  Serial1.println(
      " AUTONOMOUS BASE STATION - PICO AI NODE");
  Serial1.println(
      "================================================");

  Serial1.println(
      "Status              : ONLINE");

  Serial1.println(
      "UART                : 115200 baud");

  Serial1.println(
      "Expected Schema     : abs.v1");

  Serial1.println(
      "AI Model            : NOT YET LOADED");

  Serial1.println();

  Serial1.println(
      "Paste an ABS_JSON|{...} telemetry line");

  Serial1.println(
      "or type DEMO and press Enter.");
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  // Non-blocking line receiver.
  while (Serial1.available() > 0)
  {
    char c =
        (char)Serial1.read();

    if (c == '\r')
    {
      continue;
    }

    if (c == '\n')
    {
      if (receiveBuffer.length() > 0)
      {
        handleTelemetryLine(
            receiveBuffer);

        receiveBuffer = "";
      }

      continue;
    }

    // Prevent a malformed sender from consuming all available
    // RAM with an unterminated line.
    if (receiveBuffer.length() < 8192)
    {
      receiveBuffer += c;
    }
    else
    {
      receiveBuffer = "";

      printRejectedPacket(
          "PACKET_TOO_LARGE");
    }
  }
}
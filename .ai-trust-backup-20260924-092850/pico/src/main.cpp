#include <Arduino.h>
#include <math.h>

#include "pico_temporal_student_v1.h"

// ============================================================
// AUTONOMOUS BASE STATION
// RASPBERRY PI PICO - EMBEDDED TEMPORAL AI NODE
//
// Real workflow:
//   ESP32 -> capture 24 real sampled ABS_JSON frames
//   stop ESP32 Wokwi
//   start Pico Wokwi
//   replay captured 120-second window
//
// Embedded model:
//   24 x 33 temporal input
//   Dense(792 -> 24) + ReLU + Dense(24 -> 4)
//
// AI output:
//   NORMAL / LOCAL / UPSTREAM / MIXED
//
// Rule-derived ESP32 labels remain REFERENCE ONLY.
// They are never part of the 33 embedded AI features.
// ============================================================

static const unsigned long UART_BAUD = 115200;

static const char *TELEMETRY_PREFIX = "ABS_JSON|";
static const char *SUPPORTED_SCHEMA = "abs.v1";
static const char *AI_MODEL_NAME = "TEMPORAL_MLP_STUDENT_V1";
static const char *CONTROL_RECOMMEND_PREFIX = "PICO_RECOMMEND|";
static const char *CONTROL_RECOMMEND_SCHEMA = "pico.control.recommend.v1";
static const char *CONTROL_DECISION_PREFIX = "PICO_DECISION|";


String receiveBuffer;

float temporalWindow[
    PICO_MODEL_TIMESTEPS
][
    PICO_MODEL_FEATURES
];

uint16_t temporalCount = 0;


// ============================================================
// LIGHTWEIGHT JSON HELPERS
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


// ============================================================
// FEATURE EXTRACTION
// ============================================================
//
// Exact model feature order must match the generated header:
//
//  0 shelter_temp_c
//  1 humidity_pct
//  2 pa_temp_c
//  3 pa_temp_trend_c_per_min
//  4 pa_shelter_delta_c
//  5 vibration_rms_mps2
//  6 vibration_std_mps2
//  7 vibration_peak_to_peak_mps2
//  8 vibration_dominant_hz
//  9 dc_voltage_v
// 10 dc_current_a
// 11 dc_power_w
// 12 battery_voltage_v
// 13 battery_soc_pct
// 14 battery_soc_trend_pct_per_min
// 15 rf_forward_w
// 16 rf_reflection_ratio_pct
// 17 rf_reflected_w
// 18 vswr
// 19 return_loss_db
// 20 latency_ms
// 21 latency_jitter_ms
// 22 packet_loss_pct
// 23 rssi_dbm
// 24 rssi_drop_db
// 25 traffic_load_pct
// 26 physical_link_up
// 27 upstream_reachable
// 28 grid_available
// 29 generator_running
// 30 fan_operational
// 31 rectifier_normal
// 32 radio_operational
// ============================================================

bool extractModelFeatures(
    const String &json,
    float *features)
{
  bool physicalLinkUp = false;
  bool upstreamReachable = false;
  bool gridAvailable = false;
  bool generatorRunning = false;
  bool fanOperational = false;
  bool rectifierNormal = false;
  bool radioOperational = false;

  bool valid =
      extractFloatField(
          json,
          "shelter_temp_c",
          features[0]
      ) &&

      extractFloatField(
          json,
          "humidity_pct",
          features[1]
      ) &&

      extractFloatField(
          json,
          "pa_temp_c",
          features[2]
      ) &&

      extractFloatField(
          json,
          "pa_temp_trend_c_per_min",
          features[3]
      ) &&

      extractFloatField(
          json,
          "pa_shelter_delta_c",
          features[4]
      ) &&

      extractFloatField(
          json,
          "vibration_rms_mps2",
          features[5]
      ) &&

      extractFloatField(
          json,
          "vibration_std_mps2",
          features[6]
      ) &&

      extractFloatField(
          json,
          "vibration_peak_to_peak_mps2",
          features[7]
      ) &&

      extractFloatField(
          json,
          "vibration_dominant_hz",
          features[8]
      ) &&

      extractFloatField(
          json,
          "dc_voltage_v",
          features[9]
      ) &&

      extractFloatField(
          json,
          "dc_current_a",
          features[10]
      ) &&

      extractFloatField(
          json,
          "dc_power_w",
          features[11]
      ) &&

      extractFloatField(
          json,
          "battery_voltage_v",
          features[12]
      ) &&

      extractFloatField(
          json,
          "battery_soc_pct",
          features[13]
      ) &&

      extractFloatField(
          json,
          "battery_soc_trend_pct_per_min",
          features[14]
      ) &&

      extractFloatField(
          json,
          "rf_forward_w",
          features[15]
      ) &&

      extractFloatField(
          json,
          "rf_reflection_ratio_pct",
          features[16]
      ) &&

      extractFloatField(
          json,
          "rf_reflected_w",
          features[17]
      ) &&

      extractFloatField(
          json,
          "vswr",
          features[18]
      ) &&

      extractFloatField(
          json,
          "return_loss_db",
          features[19]
      ) &&

      extractFloatField(
          json,
          "latency_ms",
          features[20]
      ) &&

      extractFloatField(
          json,
          "latency_jitter_ms",
          features[21]
      ) &&

      extractFloatField(
          json,
          "packet_loss_pct",
          features[22]
      ) &&

      extractFloatField(
          json,
          "rssi_dbm",
          features[23]
      ) &&

      extractFloatField(
          json,
          "rssi_drop_db",
          features[24]
      ) &&

      extractFloatField(
          json,
          "traffic_load_pct",
          features[25]
      ) &&

      extractBoolField(
          json,
          "physical_link_up",
          physicalLinkUp
      ) &&

      extractBoolField(
          json,
          "upstream_reachable",
          upstreamReachable
      ) &&

      extractBoolField(
          json,
          "grid_available",
          gridAvailable
      ) &&

      extractBoolField(
          json,
          "generator_running",
          generatorRunning
      ) &&

      extractBoolField(
          json,
          "fan_operational",
          fanOperational
      ) &&

      extractBoolField(
          json,
          "rectifier_normal",
          rectifierNormal
      ) &&

      extractBoolField(
          json,
          "radio_operational",
          radioOperational
      );

  features[26] =
      physicalLinkUp
          ? 1.0f
          : 0.0f;

  features[27] =
      upstreamReachable
          ? 1.0f
          : 0.0f;

  features[28] =
      gridAvailable
          ? 1.0f
          : 0.0f;

  features[29] =
      generatorRunning
          ? 1.0f
          : 0.0f;

  features[30] =
      fanOperational
          ? 1.0f
          : 0.0f;

  features[31] =
      rectifierNormal
          ? 1.0f
          : 0.0f;

  features[32] =
      radioOperational
          ? 1.0f
          : 0.0f;

  return valid;
}


// ============================================================
// TEMPORAL WINDOW
// ============================================================

void addTemporalFrame(
    const float *features)
{
  if (
      temporalCount <
      PICO_MODEL_TIMESTEPS)
  {
    for (
        uint16_t feature = 0;
        feature <
        PICO_MODEL_FEATURES;
        feature++)
    {
      temporalWindow[
          temporalCount
      ][
          feature
      ] =
          features[
              feature
          ];
    }

    temporalCount++;

    return;
  }

  for (
      uint16_t timestep = 1;
      timestep <
      PICO_MODEL_TIMESTEPS;
      timestep++)
  {
    for (
        uint16_t feature = 0;
        feature <
        PICO_MODEL_FEATURES;
        feature++)
    {
      temporalWindow[
          timestep - 1
      ][
          feature
      ] =
          temporalWindow[
              timestep
          ][
              feature
          ];
    }
  }

  for (
      uint16_t feature = 0;
      feature <
      PICO_MODEL_FEATURES;
      feature++)
  {
    temporalWindow[
        PICO_MODEL_TIMESTEPS - 1
    ][
        feature
    ] =
        features[
            feature
        ];
  }
}


// ============================================================
// EMBEDDED MODEL INFERENCE
// ============================================================

void runEmbeddedModel(
    String &faultDomain,
    float &confidence)
{
  float hidden[
      PICO_MODEL_HIDDEN
  ];

  for (
      uint16_t h = 0;
      h <
      PICO_MODEL_HIDDEN;
      h++)
  {
    float sum =
        PICO_MODEL_B1[
            h
        ];

    for (
        uint16_t timestep = 0;
        timestep <
        PICO_MODEL_TIMESTEPS;
        timestep++)
    {
      for (
          uint16_t feature = 0;
          feature <
          PICO_MODEL_FEATURES;
          feature++)
      {
        uint16_t flatIndex =
            timestep *
            PICO_MODEL_FEATURES +
            feature;

        float scale =
            PICO_MODEL_SCALE[
                flatIndex
            ];

        if (
            fabsf(
                scale
            ) <
            1e-12f)
        {
          scale =
              1.0f;
        }

        float normalized =
            (
                temporalWindow[
                    timestep
                ][
                    feature
                ] -
                PICO_MODEL_MEAN[
                    flatIndex
                ]
            ) /
            scale;

        sum +=
            normalized *
            PICO_MODEL_W1[
                h *
                PICO_MODEL_INPUTS +
                flatIndex
            ];
      }
    }

    hidden[
        h
    ] =
        (
            sum >
            0.0f
        )
            ? sum
            : 0.0f;
  }

  float logits[
      PICO_MODEL_CLASSES
  ];

  float maxLogit =
      -1.0e30f;

  for (
      uint16_t klass = 0;
      klass <
      PICO_MODEL_CLASSES;
      klass++)
  {
    float sum =
        PICO_MODEL_B2[
            klass
        ];

    for (
        uint16_t h = 0;
        h <
        PICO_MODEL_HIDDEN;
        h++)
    {
      sum +=
          hidden[
              h
          ] *
          PICO_MODEL_W2[
              klass *
              PICO_MODEL_HIDDEN +
              h
          ];
    }

    logits[
        klass
    ] =
        sum;

    if (
        sum >
        maxLogit)
    {
      maxLogit =
          sum;
    }
  }

  float probabilities[
      PICO_MODEL_CLASSES
  ];

  float denominator =
      0.0f;

  for (
      uint16_t klass = 0;
      klass <
      PICO_MODEL_CLASSES;
      klass++)
  {
    probabilities[
        klass
    ] =
        expf(
            logits[
                klass
            ] -
            maxLogit
        );

    denominator +=
        probabilities[
            klass
        ];
  }

  uint16_t bestClass =
      0;

  float bestProbability =
      0.0f;

  for (
      uint16_t klass = 0;
      klass <
      PICO_MODEL_CLASSES;
      klass++)
  {
    float probability =
        (
            denominator >
            0.0f
        )
            ? (
                probabilities[
                    klass
                ] /
                denominator
            )
            : 0.0f;

    if (
        klass == 0 ||
        probability >
        bestProbability)
    {
      bestProbability =
          probability;

      bestClass =
          klass;
    }
  }

  faultDomain =
      PICO_MODEL_LABELS[
          bestClass
      ];

  confidence =
      bestProbability;
}


// ============================================================
// EMBEDDED CONTROL RECOMMENDATION
// ============================================================
//
// This is a conservative deterministic policy driven by the
// embedded AI fault-domain result. It is NOT a separately
// learned energy model and it does not bypass ESP32 safety
// guardrails.
// ============================================================

String recommendMode(
    const String &faultDomain,
    const float *latestFeatures)
{
  float batterySoc =
      latestFeatures[
          13
      ];

  float traffic =
      latestFeatures[
          25
      ];

  bool gridAvailable =
      latestFeatures[
          28
      ] >
      0.5f;

  bool generatorRunning =
      latestFeatures[
          29
      ] >
      0.5f;

  if (
      !gridAvailable &&
      !generatorRunning &&
      batterySoc <=
          25.0f)
  {
    return "EMERGENCY";
  }

  if (
      faultDomain ==
          "LOCAL" ||
      faultDomain ==
          "MIXED")
  {
    return "FULL";
  }

  if (
      faultDomain ==
      "UPSTREAM")
  {
    return "ECO";
  }

  if (
      traffic >=
      65.0f)
  {
    return "FULL";
  }

  if (
      traffic <
      15.0f)
  {
    return "REDUCED";
  }

  return "ECO";
}


// ============================================================
// LIVE CONTROL DECISION
// ============================================================
//
// Laptop AI recommends. Pico converts recommendations plus
// power context into deterministic control decisions. ESP32
// safety guardrails remain the final authority.
// ============================================================

void printControlRejected(
    const String &reason)
{
  Serial1.print(CONTROL_DECISION_PREFIX);
  Serial1.print(
      "{\"schema\":\"pico.control.decision.v1\","
      "\"accepted\":false,\"reason\":\""
  );
  Serial1.print(reason);
  Serial1.println("\"}");
}


void printControlDecision(
    const String &modeDecision,
    const String &powerSourceDecision,
    const String &generatorAction,
    const String &decisionReason,
    const String &faultDomain,
    float confidence)
{
  Serial1.print(CONTROL_DECISION_PREFIX);
  Serial1.print(
      "{\"schema\":\"pico.control.decision.v1\","
      "\"accepted\":true,\"mode_decision\":\""
  );
  Serial1.print(modeDecision);
  Serial1.print("\",\"power_source_decision\":\"");
  Serial1.print(powerSourceDecision);
  Serial1.print("\",\"generator_action\":\"");
  Serial1.print(generatorAction);
  Serial1.print("\",\"decision_reason\":\"");
  Serial1.print(decisionReason);
  Serial1.print("\",\"fault_domain\":\"");
  Serial1.print(faultDomain);
  Serial1.print("\",\"confidence\":");
  Serial1.print(confidence, 6);
  Serial1.println("}");
}


void handleControlRecommendationLine(
    String line)
{
  line.trim();

  String json =
      line.substring(
          strlen(
              CONTROL_RECOMMEND_PREFIX
          )
      );

  String schema;
  String recommendedMode;
  String recommendedPowerSource;
  String generatorRecommendation;
  String faultDomain;
  String recommendationReason;

  float confidence = 0.0f;
  float batterySoc = 0.0f;
  float trafficLoad = 0.0f;

  bool gridAvailable = false;
  bool generatorRunning = false;
  bool anomalyFlag = false;

  bool valid =
      extractStringField(json, "schema", schema) &&
      extractStringField(json, "recommended_mode", recommendedMode) &&
      extractStringField(json, "recommended_power_source", recommendedPowerSource) &&
      extractStringField(json, "generator_recommendation", generatorRecommendation) &&
      extractStringField(json, "fault_domain", faultDomain) &&
      extractStringField(json, "reason", recommendationReason) &&
      extractFloatField(json, "domain_confidence", confidence) &&
      extractFloatField(json, "battery_soc_pct", batterySoc) &&
      extractFloatField(json, "traffic_load_pct", trafficLoad) &&
      extractBoolField(json, "grid_available", gridAvailable) &&
      extractBoolField(json, "generator_running", generatorRunning) &&
      extractBoolField(json, "anomaly_flag", anomalyFlag);

  if (!valid || schema != CONTROL_RECOMMEND_SCHEMA)
  {
    printControlRejected("INVALID_RECOMMENDATION");
    return;
  }

  String modeDecision = recommendedMode;
  String powerSourceDecision = recommendedPowerSource;
  String generatorAction = "HOLD";
  String decisionReason = recommendationReason;

  if (!gridAvailable && batterySoc <= 25.0f)
  {
    modeDecision = "EMERGENCY";
    powerSourceDecision = "GENERATOR";
    generatorAction = "START";
    decisionReason = "CRITICAL BACKUP ENERGY";
  }
  else if (
      recommendedPowerSource == "GENERATOR" ||
      generatorRecommendation == "START")
  {
    powerSourceDecision = "GENERATOR";
    generatorAction = "START";
    decisionReason = "PICO ACCEPTED GENERATOR RECOMMENDATION";
  }
  else if (gridAvailable)
  {
    powerSourceDecision = "GRID";
    generatorAction = "STOP";
    decisionReason = "GRID AVAILABLE";
  }
  else if (recommendedPowerSource == "BATTERY")
  {
    powerSourceDecision = "BATTERY";
    generatorAction = "STOP";
    decisionReason = "BATTERY BACKUP SELECTED";
  }
  else
  {
    generatorAction = generatorRunning ? "HOLD" : "STOP";
  }

  if (anomalyFlag && modeDecision == "REDUCED")
  {
    modeDecision = "ECO";
    decisionReason = "ANOMALY CONSERVATIVE ECO";
  }

  printControlDecision(
      modeDecision,
      powerSourceDecision,
      generatorAction,
      decisionReason,
      faultDomain,
      confidence
  );
}


// ============================================================
// RESPONSE
// ============================================================

void printRejectedPacket(
    const String &reason)
{
  Serial1.print(
      "PICO_RESULT|{"
      "\"schema\":\"pico.ai.v1\","
      "\"telemetry_ok\":false,"
      "\"reason\":\""
  );

  Serial1.print(
      reason
  );

  Serial1.println(
      "\"}"
  );
}


void printResult(
    const String &sourceFaultLabel,
    const String &sourceOperatingMode,
    bool ready,
    const String &faultDomain,
    float confidence,
    const String &recommendedMode)
{
  Serial1.print(
      "PICO_RESULT|{"
      "\"schema\":\"pico.ai.v1\","
      "\"telemetry_ok\":true,"
      "\"source_schema\":\"abs.v1\","
      "\"window_count\":"
  );

  Serial1.print(
      temporalCount
  );

  Serial1.print(
      ",\"window_ready\":"
  );

  Serial1.print(
      ready
          ? "true"
          : "false"
  );

  Serial1.print(
      ",\"ai_model\":\""
  );

  Serial1.print(
      AI_MODEL_NAME
  );

  Serial1.print(
      "\",\"source_fault_label\":\""
  );

  Serial1.print(
      sourceFaultLabel
  );

  Serial1.print(
      "\",\"source_operating_mode\":\""
  );

  Serial1.print(
      sourceOperatingMode
  );

  Serial1.print(
      "\""
  );

  if (ready)
  {
    Serial1.print(
        ",\"fault_domain\":\""
    );

    Serial1.print(
        faultDomain
    );

    Serial1.print(
        "\",\"confidence\":"
    );

    Serial1.print(
        confidence,
        6
    );

    Serial1.print(
        ",\"recommended_mode\":\""
    );

    Serial1.print(
        recommendedMode
    );

    Serial1.print(
        "\",\"final_mode_authority\":"
        "\"ESP32_DETERMINISTIC_GUARDRAILS\""
    );
  }

  Serial1.println(
      "}"
  );
}


// ============================================================
// TELEMETRY HANDLER
// ============================================================

void handleTelemetryLine(
    String line)
{
  line.trim();

  if (
      line.length() ==
      0)
  {
    return;
  }

  if (
      !line.startsWith(
          TELEMETRY_PREFIX
      ))
  {
    printRejectedPacket(
        "INVALID_PREFIX"
    );

    return;
  }

  String json =
      line.substring(
          strlen(
              TELEMETRY_PREFIX
          )
      );

  String schema;
  String sourceFaultLabel =
      "UNKNOWN";

  String sourceOperatingMode =
      "UNKNOWN";

  bool schemaOk =
      extractStringField(
          json,
          "schema",
          schema
      );

  if (
      !schemaOk ||
      schema !=
          SUPPORTED_SCHEMA)
  {
    printRejectedPacket(
        "UNSUPPORTED_SCHEMA"
    );

    return;
  }

  float features[
      PICO_MODEL_FEATURES
  ];

  if (
      !extractModelFeatures(
          json,
          features
      ))
  {
    printRejectedPacket(
        "MISSING_MODEL_FEATURE"
    );

    return;
  }

  // These are comparison/reference fields only.
  // They never enter the model feature vector.
  extractStringField(
      json,
      "fault_label",
      sourceFaultLabel
  );

  extractStringField(
      json,
      "operating_mode",
      sourceOperatingMode
  );

  addTemporalFrame(
      features
  );

  digitalWrite(
      LED_BUILTIN,
      HIGH
  );

  bool ready =
      temporalCount >=
      PICO_MODEL_TIMESTEPS;

  String faultDomain =
      "WARMUP";

  float confidence =
      0.0f;

  String recommendedMode =
      "PENDING";

  if (ready)
  {
    runEmbeddedModel(
        faultDomain,
        confidence
    );

    recommendedMode =
        recommendMode(
            faultDomain,
            features
        );
  }

  Serial1.print(
      "[PICO] window="
  );

  Serial1.print(
      temporalCount
  );

  Serial1.print(
      "/"
  );

  Serial1.print(
      PICO_MODEL_TIMESTEPS
  );

  if (ready)
  {
    Serial1.print(
        " | AI="
    );

    Serial1.print(
        faultDomain
    );

    Serial1.print(
        " ("
    );

    Serial1.print(
        confidence,
        4
    );

    Serial1.print(
        ") | mode="
    );

    Serial1.println(
        recommendedMode
    );
  }
  else
  {
    Serial1.println(
        " | AI WARMUP"
    );
  }

  printResult(
      sourceFaultLabel,
      sourceOperatingMode,
      ready,
      faultDomain,
      confidence,
      recommendedMode
  );

  digitalWrite(
      LED_BUILTIN,
      LOW
  );
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
  pinMode(
      LED_BUILTIN,
      OUTPUT
  );

  digitalWrite(
      LED_BUILTIN,
      LOW
  );

  Serial1.begin(
      UART_BAUD
  );

  delay(
      500
  );

  Serial1.println();
  Serial1.println(
      "================================================"
  );
  Serial1.println(
      " AUTONOMOUS BASE STATION - PICO TEMPORAL AI"
  );
  Serial1.println(
      "================================================"
  );

  Serial1.println(
      "Status              : ONLINE"
  );

  Serial1.println(
      "Expected Schema     : abs.v1"
  );

  Serial1.print(
      "AI Model            : "
  );

  Serial1.println(
      AI_MODEL_NAME
  );

  Serial1.print(
      "Temporal Window     : "
  );

  Serial1.print(
      PICO_MODEL_TIMESTEPS
  );

  Serial1.print(
      " x "
  );

  Serial1.println(
      PICO_MODEL_FEATURES
  );

  Serial1.println(
      "Final Mode Authority: ESP32 DETERMINISTIC GUARDRAILS"
  );

  Serial1.println(
      "Live Control        : AI RECOMMENDATION -> PICO DECISION -> ESP32 ACTUATION"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  while (
      Serial1.available() >
      0)
  {
    char c =
        (
            char
        )Serial1.read();

    if (
        c == '\r')
    {
      continue;
    }

    if (
        c == '\n')
    {
      if (
          receiveBuffer.length() >
          0)
      {
        if (
            receiveBuffer.startsWith(
                CONTROL_RECOMMEND_PREFIX
            ))
        {
          handleControlRecommendationLine(
              receiveBuffer
          );
        }
        else
        {
          handleTelemetryLine(
              receiveBuffer
          );
        }

        receiveBuffer =
            "";
      }

      continue;
    }

    if (
        receiveBuffer.length() <
        8192)
    {
      receiveBuffer +=
          c;
    }
    else
    {
      receiveBuffer =
          "";

      printRejectedPacket(
          "PACKET_TOO_LARGE"
      );
    }
  }
}
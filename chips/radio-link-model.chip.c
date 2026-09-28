#include "wokwi-api.h"

static pin_t rssi_pin;
static pin_t rf_forward_pin;
static pin_t rf_reflected_pin;
static pin_t radio_ok_pin;

static uint32_t tx_power_attr;
static uint32_t path_loss_attr;
static uint32_t interference_attr;
static uint32_t mismatch_attr;
static uint32_t fading_attr;
static uint32_t radio_fault_attr;
static timer_t update_timer;
static uint32_t prng_state = 0x2c91a5b7u;

static float clampf_local(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static float random_signed(void) {
  prng_state = prng_state * 1103515245u + 12345u;
  uint32_t sample = (prng_state >> 8) & 0x00ffffffu;
  float unit = (float)sample / 16777215.0f;
  return unit * 2.0f - 1.0f;
}

static void update_radio(void *user_data) {
  (void)user_data;

  float tx_power_w = clampf_local(attr_read_float(tx_power_attr), 0.0f, 100.0f);
  float path_loss_db = clampf_local(attr_read_float(path_loss_attr), 40.0f, 120.0f);
  float interference_db = clampf_local(attr_read_float(interference_attr), 0.0f, 30.0f);
  float mismatch_pct = clampf_local(attr_read_float(mismatch_attr), 0.0f, 90.0f);
  float fading_depth_db = clampf_local(attr_read_float(fading_attr), 0.0f, 20.0f);
  bool radio_fault = attr_read(radio_fault_attr) >= 1u;

  float rssi_dbm;
  float forward_w;
  float reflected_w;

  if (radio_fault) {
    rssi_dbm = -120.0f;
    forward_w = 0.0f;
    reflected_w = 0.0f;
  } else {
    float fading_db = random_signed() * fading_depth_db;

    rssi_dbm = -45.0f
      - (path_loss_db - 55.0f) * 0.80f
      - interference_db * 1.20f
      + fading_db;
    rssi_dbm = clampf_local(rssi_dbm, -120.0f, -45.0f);

    float interference_derating = 1.0f - (interference_db / 30.0f) * 0.10f;
    forward_w = tx_power_w * clampf_local(interference_derating, 0.80f, 1.0f);
    reflected_w = forward_w * (mismatch_pct / 100.0f);
  }

  float rssi_voltage = ((rssi_dbm + 120.0f) / 75.0f) * 5.0f;
  float forward_voltage = (forward_w / 100.0f) * 5.0f;
  float reflected_voltage = (reflected_w / 100.0f) * 5.0f;

  pin_dac_write(rssi_pin, clampf_local(rssi_voltage, 0.0f, 5.0f));
  pin_dac_write(rf_forward_pin, clampf_local(forward_voltage, 0.0f, 5.0f));
  pin_dac_write(rf_reflected_pin, clampf_local(reflected_voltage, 0.0f, 5.0f));
  pin_write(radio_ok_pin, radio_fault ? LOW : HIGH);
}

void chip_init(void) {
  rssi_pin = pin_init("RSSI", ANALOG);
  rf_forward_pin = pin_init("RF_FWD", ANALOG);
  rf_reflected_pin = pin_init("RF_REFL", ANALOG);
  radio_ok_pin = pin_init("RADIO_OK", OUTPUT_LOW);

  tx_power_attr = attr_init_float("txPowerW", 80.0f);
  path_loss_attr = attr_init_float("pathLossDb", 65.0f);
  interference_attr = attr_init_float("interferenceDb", 2.0f);
  mismatch_attr = attr_init_float("antennaMismatchPct", 1.5f);
  fading_attr = attr_init_float("fadingDepthDb", 1.0f);
  radio_fault_attr = attr_init("radioFault", 0u);

  const timer_config_t config = {
    .user_data = 0,
    .callback = update_radio,
    .reserved = {0}
  };
  update_timer = timer_init(&config);

  update_radio(0);
  timer_start(update_timer, 50000u, true);
}

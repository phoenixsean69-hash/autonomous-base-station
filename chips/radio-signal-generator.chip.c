#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  pin_t carrier_in;
  pin_t pathloss;
  pin_t interference;
  pin_t mismatch;
  pin_t radio_fault;
  pin_t rssi_out;
  pin_t rf_fwd;
  pin_t rf_refl;
  pin_t radio_ok;
  pin_t activity;
  timer_t timer;
  uint32_t phase;
  bool carrier_seen;
} chip_state_t;

static float clampf_local(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

static void carrier_edge(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *s = (chip_state_t *)user_data;
  if (value == HIGH) {
    s->carrier_seen = true;
    pin_write(s->activity, (s->phase & 1u) ? HIGH : LOW);
  }
}

static void update_signal(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;
  s->phase++;

  bool pathloss = pin_read(s->pathloss) == HIGH;
  bool interference = pin_read(s->interference) == HIGH;
  bool mismatch = pin_read(s->mismatch) == HIGH;
  bool fault = pin_read(s->radio_fault) == HIGH;

  float fade_pattern[8] = {-1.5f,-0.6f,0.4f,1.2f,0.7f,-0.2f,-1.0f,0.2f};
  float rssi_dbm = -60.0f + fade_pattern[s->phase & 7u];
  if (pathloss) rssi_dbm -= 24.0f;
  if (interference) rssi_dbm -= 12.0f;
  if (fault) rssi_dbm = -112.0f;
  rssi_dbm = clampf_local(rssi_dbm, -120.0f, -45.0f);

  float forward_w = fault ? 3.0f : 80.0f;
  if (interference && !fault) forward_w -= 4.0f;
  float reflected_w = mismatch ? 18.0f : 1.2f;
  if (fault) reflected_w = 2.5f;
  if (reflected_w >= forward_w) reflected_w = forward_w * 0.8f;

  float rssi_voltage = ((rssi_dbm + 120.0f) / 75.0f) * 5.0f;
  float fwd_voltage = (forward_w / 100.0f) * 5.0f;
  float refl_voltage = (reflected_w / 100.0f) * 5.0f;

  pin_dac_write(s->rssi_out, clampf_local(rssi_voltage, 0.0f, 5.0f));
  pin_dac_write(s->rf_fwd, clampf_local(fwd_voltage, 0.0f, 5.0f));
  pin_dac_write(s->rf_refl, clampf_local(refl_voltage, 0.0f, 5.0f));
  pin_write(s->radio_ok, fault ? LOW : HIGH);
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;
  s->carrier_in = pin_init("CARRIER_IN", INPUT_PULLDOWN);
  s->pathloss = pin_init("PATHLOSS", INPUT_PULLDOWN);
  s->interference = pin_init("INTERFERENCE", INPUT_PULLDOWN);
  s->mismatch = pin_init("MISMATCH", INPUT_PULLDOWN);
  s->radio_fault = pin_init("RADIO_FAULT", INPUT_PULLDOWN);
  s->rssi_out = pin_init("RSSI_OUT", ANALOG);
  s->rf_fwd = pin_init("RF_FWD", ANALOG);
  s->rf_refl = pin_init("RF_REFL", ANALOG);
  s->radio_ok = pin_init("RADIO_OK", OUTPUT_HIGH);
  s->activity = pin_init("ACTIVITY", OUTPUT_LOW);

  const pin_watch_config_t carrier_watch = {.user_data=s, .edge=RISING, .pin_change=carrier_edge};
  pin_watch(s->carrier_in, &carrier_watch);

  const timer_config_t config = {.user_data=s, .callback=update_signal, .reserved={0}};
  s->timer = timer_init(&config);
  update_signal(s);
  timer_start(s->timer, 50000u, true);
}

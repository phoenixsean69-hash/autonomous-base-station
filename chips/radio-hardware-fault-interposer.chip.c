#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  pin_t rf_fwd_in;
  pin_t rf_refl_in;
  pin_t radio_ok_in;
  pin_t rf_fwd_out;
  pin_t rf_refl_out;
  pin_t radio_ok_out;
  pin_t fault_active;
  uint32_t fault_enable_attr;
  uint32_t severity_attr;
  timer_t timer;
} chip_state_t;

static float clampf_local(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static void refresh(void *user_data) {
  chip_state_t *s = (chip_state_t *)user_data;

  float fwd_in_v = clampf_local(pin_adc_read(s->rf_fwd_in), 0.0f, 5.0f);
  float refl_in_v = clampf_local(pin_adc_read(s->rf_refl_in), 0.0f, 5.0f);
  bool source_ok = pin_read(s->radio_ok_in) == HIGH;

  bool fault_enabled = attr_read(s->fault_enable_attr) >= 1u;
  float severity = clampf_local((float)attr_read(s->severity_attr) / 100.0f, 0.0f, 1.0f);

  float fwd_out_v = fwd_in_v;
  float refl_out_v = refl_in_v;
  bool radio_ok = source_ok;

  if (fault_enabled) {
    /*
     * Align the injected symptom with the trained RADIO_FAULT pattern:
     * forward RF power falls strongly while the reflected/forward ratio
     * stays approximately unchanged.  That avoids disguising a radio
     * hardware failure as an antenna mismatch.
     *
     * 0% severity  -> 65% of normal RF power
     * 100% severity -> 15% of normal RF power
     */
    float power_factor = 0.65f - (0.50f * severity);
    power_factor = clampf_local(power_factor, 0.15f, 0.65f);

    fwd_out_v = fwd_in_v * power_factor;
    refl_out_v = refl_in_v * power_factor;
    radio_ok = false;
  }

  pin_dac_write(s->rf_fwd_out, clampf_local(fwd_out_v, 0.0f, 5.0f));
  pin_dac_write(s->rf_refl_out, clampf_local(refl_out_v, 0.0f, 5.0f));
  pin_write(s->radio_ok_out, radio_ok ? HIGH : LOW);
  pin_write(s->fault_active, fault_enabled ? HIGH : LOW);
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;

  s->rf_fwd_in = pin_init("RF_FWD_IN", ANALOG);
  s->rf_refl_in = pin_init("RF_REFL_IN", ANALOG);
  s->radio_ok_in = pin_init("RADIO_OK_IN", INPUT_PULLDOWN);

  s->rf_fwd_out = pin_init("RF_FWD_OUT", ANALOG);
  s->rf_refl_out = pin_init("RF_REFL_OUT", ANALOG);
  s->radio_ok_out = pin_init("RADIO_OK_OUT", OUTPUT_LOW);
  s->fault_active = pin_init("FAULT_ACTIVE", OUTPUT_LOW);

  s->fault_enable_attr = attr_init("faultEnable", 0u);
  s->severity_attr = attr_init("severityPct", 100u);

  const timer_config_t cfg = {
    .user_data = s,
    .callback = refresh,
    .reserved = {0},
  };

  s->timer = timer_init(&cfg);
  refresh(s);
  timer_start(s->timer, 50000u, true);
}

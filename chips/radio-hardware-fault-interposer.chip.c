#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  pin_t rf_fwd_in;
  pin_t rf_refl_in;
  pin_t radio_ok_in;
  pin_t fault_enable;
  pin_t severity_in;
  pin_t rf_fwd_out;
  pin_t rf_refl_out;
  pin_t radio_ok_out;
  pin_t fault_active;
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
  bool fault_enabled = pin_read(s->fault_enable) == HIGH;
  float severity_v = clampf_local(pin_adc_read(s->severity_in), 0.0f, 3.3f);
  float severity = clampf_local(severity_v / 3.3f, 0.0f, 1.0f);

  float fwd_out_v = fwd_in_v;
  float refl_out_v = refl_in_v;
  bool radio_ok = source_ok;

  if (fault_enabled) {
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
  s->fault_enable = pin_init("FAULT_ENABLE", INPUT_PULLDOWN);
  s->severity_in = pin_init("SEVERITY_IN", ANALOG);
  s->rf_fwd_out = pin_init("RF_FWD_OUT", ANALOG);
  s->rf_refl_out = pin_init("RF_REFL_OUT", ANALOG);
  s->radio_ok_out = pin_init("RADIO_OK_OUT", OUTPUT_LOW);
  s->fault_active = pin_init("FAULT_ACTIVE", OUTPUT_LOW);
  const timer_config_t cfg = {.user_data=s, .callback=refresh, .reserved={0}};
  s->timer = timer_init(&cfg);
  refresh(s);
  timer_start(s->timer, 50000u, true);
}

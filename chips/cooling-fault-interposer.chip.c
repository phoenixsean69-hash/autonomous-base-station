#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Cooling Fan Fault Interposer
 *
 * Purpose:
 *   Sit between the normal fan-health source and ESP32 GPIO18.
 *
 * Healthy / bypass:
 *   FAN_OUT follows FAN_IN.
 *
 * Injected complete fan failure:
 *   FAULT_ENABLE = HIGH forces FAN_OUT LOW.
 *
 * IMPORTANT:
 *   The ESP32 is not told "COOLING_FAULT".
 *   It only observes fan_operational=false through GPIO18.
 */

typedef struct {
  pin_t fan_in;
  pin_t fault_enable;
  pin_t fan_out;
  pin_t fault_active;
} chip_state_t;

static void update_outputs(chip_state_t *s) {
  bool source_healthy =
      pin_read(s->fan_in) == HIGH;

  bool inject_failure =
      pin_read(s->fault_enable) == HIGH;

  pin_write(
      s->fault_active,
      inject_failure ? HIGH : LOW
  );

  pin_write(
      s->fan_out,
      (
          source_healthy &&
          !inject_failure
      )
          ? HIGH
          : LOW
  );
}

static void input_changed(
    void *user_data,
    pin_t pin,
    uint32_t value) {
  (void)pin;
  (void)value;

  update_outputs(
      (chip_state_t *)user_data
  );
}

void chip_init(void) {
  static chip_state_t state;
  chip_state_t *s = &state;

  s->fan_in =
      pin_init(
          "FAN_IN",
          INPUT_PULLDOWN
      );

  s->fault_enable =
      pin_init(
          "FAULT_ENABLE",
          INPUT_PULLDOWN
      );

  s->fan_out =
      pin_init(
          "FAN_OUT",
          OUTPUT_LOW
      );

  s->fault_active =
      pin_init(
          "FAULT_ACTIVE",
          OUTPUT_LOW
      );

  const pin_watch_config_t watch = {
      .user_data = s,
      .edge = BOTH,
      .pin_change = input_changed,
  };

  pin_watch(
      s->fan_in,
      &watch
  );

  pin_watch(
      s->fault_enable,
      &watch
  );

  update_outputs(s);
}

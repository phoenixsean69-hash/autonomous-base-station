#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Cooling Fan Fault Interposer V2
 *
 * The normal fan-health signal enters FAN_IN.
 * The chip's own Wokwi control injects the failure.
 *
 * faultEnable = 0 -> transparent pass-through
 * faultEnable = 1 -> force FAN_OUT LOW
 *
 * The ESP32 is never given a COOLING_FAULT label.
 * It only observes the resulting fan health on GPIO18.
 */

typedef struct {
  pin_t fan_in;
  pin_t fan_out;
  pin_t fault_active;

  uint32_t fault_enable_attr;
  timer_t refresh_timer;
} chip_state_t;

static void update_outputs(
    chip_state_t *state)
{
  bool source_healthy =
      pin_read(
          state->fan_in
      ) == HIGH;

  bool inject_failure =
      attr_read(
          state->fault_enable_attr
      ) >= 1u;

  pin_write(
      state->fault_active,
      inject_failure
          ? HIGH
          : LOW
  );

  pin_write(
      state->fan_out,
      (
          source_healthy &&
          !inject_failure
      )
          ? HIGH
          : LOW
  );
}


static void refresh(
    void *user_data)
{
  update_outputs(
      (chip_state_t *)user_data
  );
}


void chip_init(void)
{
  static chip_state_t state;

  state.fan_in =
      pin_init(
          "FAN_IN",
          INPUT_PULLDOWN
      );

  state.fan_out =
      pin_init(
          "FAN_OUT",
          OUTPUT_LOW
      );

  state.fault_active =
      pin_init(
          "FAULT_ACTIVE",
          OUTPUT_LOW
      );

  state.fault_enable_attr =
      attr_init(
          "faultEnable",
          0u
      );

  const timer_config_t timer_config = {
      .user_data = &state,
      .callback = refresh,
      .reserved = {0},
  };

  state.refresh_timer =
      timer_init(
          &timer_config
      );

  update_outputs(
      &state
  );

  // Poll the Wokwi control every 50 ms.
  timer_start(
      state.refresh_timer,
      50000u,
      true
  );
}

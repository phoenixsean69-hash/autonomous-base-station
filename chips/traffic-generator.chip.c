#include "wokwi-api.h"

static pin_t traffic_pin;
static uint32_t users_attr;
static uint32_t demand_attr;
static uint32_t capacity_attr;
static uint32_t burstiness_attr;
static timer_t update_timer;
static uint32_t prng_state = 0x7b31f2c9u;

static float clampf_local(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static float random_unit(void) {
  prng_state = prng_state * 1664525u + 1013904223u;
  uint32_t sample = (prng_state >> 8) & 0x00ffffffu;
  return (float)sample / 16777215.0f;
}

static void update_traffic(void *user_data) {
  (void)user_data;

  float active_users = clampf_local(attr_read_float(users_attr), 0.0f, 500.0f);
  float average_demand_pct = clampf_local(attr_read_float(demand_attr), 0.0f, 100.0f);
  float capacity_users = clampf_local(attr_read_float(capacity_attr), 25.0f, 500.0f);
  float burstiness_pct = clampf_local(attr_read_float(burstiness_attr), 0.0f, 100.0f);

  float occupancy = active_users / capacity_users;
  float demand_factor = 0.15f + 0.85f * (average_demand_pct / 100.0f);
  float base_load_pct = occupancy * demand_factor * 100.0f;
  float burst_pct = burstiness_pct * random_unit() * 0.30f;
  float traffic_pct = clampf_local(base_load_pct + burst_pct, 0.0f, 100.0f);

  pin_dac_write(traffic_pin, (traffic_pct / 100.0f) * 5.0f);
}

void chip_init(void) {
  traffic_pin = pin_init("TRAFFIC", ANALOG);

  users_attr = attr_init_float("activeUsers", 80.0f);
  demand_attr = attr_init_float("averageDemandPct", 45.0f);
  capacity_attr = attr_init_float("cellCapacityUsers", 300.0f);
  burstiness_attr = attr_init_float("burstinessPct", 10.0f);

  const timer_config_t config = {
    .user_data = 0,
    .callback = update_traffic,
    .reserved = {0}
  };
  update_timer = timer_init(&config);

  update_traffic(0);
  timer_start(update_timer, 100000u, true);
}

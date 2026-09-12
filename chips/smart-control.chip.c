#include "wokwi-api.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_WIDTH  120
#define SCREEN_HEIGHT 120

/*
 * CONTROL MODES
 *
 * 0 = DC BUS VOLTAGE   0..60 V
 * 1 = DC BUS CURRENT   0..30 A
 * 2 = BATTERY VOLTAGE  0..15 V
 * 3 = RF POWER         0..100 W
 * 4 = BACKHAUL STRESS  0..100 %
 *
 * Each chip instance has:
 *   - its own Knob / Slider control
 *   - its own Manual Value text field
 *
 * Manual Value blank  -> knob controls output
 * Manual Value number -> typed value controls output
 */

typedef struct
{
  pin_t out_pin;

  uint32_t position_attr;
  uint32_t mode_attr;
  string_t manual_value_attr;

  buffer_t framebuffer;
  uint32_t width;
  uint32_t height;

  timer_t refresh_timer;

  uint32_t pixels[
      SCREEN_WIDTH *
      SCREEN_HEIGHT
  ];

} chip_state_t;

static uint32_t rgba(
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
  return
      ((uint32_t)r) |
      ((uint32_t)g << 8) |
      ((uint32_t)b << 16) |
      ((uint32_t)a << 24);
}

static void set_pixel(
    chip_state_t *state,
    int x,
    int y,
    uint32_t color)
{
  if (
      x < 0 ||
      y < 0 ||
      x >= SCREEN_WIDTH ||
      y >= SCREEN_HEIGHT)
  {
    return;
  }

  state->pixels[
      y * SCREEN_WIDTH + x
  ] = color;
}

static void clear_display(
    chip_state_t *state,
    uint32_t color)
{
  for (
      int i = 0;
      i < SCREEN_WIDTH * SCREEN_HEIGHT;
      i++)
  {
    state->pixels[i] =
        color;
  }
}

static void fill_rect(
    chip_state_t *state,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t color)
{
  if (x0 > x1)
  {
    int temp = x0;
    x0 = x1;
    x1 = temp;
  }

  if (y0 > y1)
  {
    int temp = y0;
    y0 = y1;
    y1 = temp;
  }

  for (
      int y = y0;
      y <= y1;
      y++)
  {
    for (
        int x = x0;
        x <= x1;
        x++)
    {
      set_pixel(
          state,
          x,
          y,
          color
      );
    }
  }
}

static void fill_circle(
    chip_state_t *state,
    int cx,
    int cy,
    int radius,
    uint32_t color)
{
  for (
      int y = -radius;
      y <= radius;
      y++)
  {
    for (
        int x = -radius;
        x <= radius;
        x++)
    {
      if (
          x * x +
          y * y <=
          radius * radius)
      {
        set_pixel(
            state,
            cx + x,
            cy + y,
            color
        );
      }
    }
  }
}

static void draw_line(
    chip_state_t *state,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t color)
{
  int dx =
      abs(x1 - x0);

  int sx =
      x0 < x1
          ? 1
          : -1;

  int dy =
      -abs(y1 - y0);

  int sy =
      y0 < y1
          ? 1
          : -1;

  int error =
      dx + dy;

  while (true)
  {
    set_pixel(
        state,
        x0,
        y0,
        color
    );

    if (
        x0 == x1 &&
        y0 == y1)
    {
      break;
    }

    int e2 =
        2 * error;

    if (e2 >= dy)
    {
      error += dy;
      x0 += sx;
    }

    if (e2 <= dx)
    {
      error += dx;
      y0 += sy;
    }
  }
}

static void get_glyph(
    char c,
    uint8_t rows[7])
{
  memset(
      rows,
      0,
      7
  );

  switch (c)
  {
    case '0':
    {
      uint8_t g[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case '1':
    {
      uint8_t g[7] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case '2':
    {
      uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
      memcpy(rows, g, 7);
      break;
    }

    case '3':
    {
      uint8_t g[7] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
      memcpy(rows, g, 7);
      break;
    }

    case '4':
    {
      uint8_t g[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
      memcpy(rows, g, 7);
      break;
    }

    case '5':
    {
      uint8_t g[7] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
      memcpy(rows, g, 7);
      break;
    }

    case '6':
    {
      uint8_t g[7] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case '7':
    {
      uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
      memcpy(rows, g, 7);
      break;
    }

    case '8':
    {
      uint8_t g[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case '9':
    {
      uint8_t g[7] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case '.':
    {
      uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
      memcpy(rows, g, 7);
      break;
    }

    case '-':
    {
      uint8_t g[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
      memcpy(rows, g, 7);
      break;
    }

    case 'V':
    {
      uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
      memcpy(rows, g, 7);
      break;
    }

    case 'A':
    {
      uint8_t g[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
      memcpy(rows, g, 7);
      break;
    }

    case 'W':
    {
      uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A};
      memcpy(rows, g, 7);
      break;
    }

    case 'N':
    {
      uint8_t g[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
      memcpy(rows, g, 7);
      break;
    }

    case 'D':
    {
      uint8_t g[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
      memcpy(rows, g, 7);
      break;
    }

    case 'C':
    {
      uint8_t g[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case 'O':
    {
      uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
      memcpy(rows, g, 7);
      break;
    }

    case 'M':
    {
      uint8_t g[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
      memcpy(rows, g, 7);
      break;
    }

    case 'K':
    {
      uint8_t g[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
      memcpy(rows, g, 7);
      break;
    }

    case '%':
    {
      uint8_t g[7] = {0x19,0x1A,0x04,0x08,0x16,0x13,0x00};
      memcpy(rows, g, 7);
      break;
    }

    case ' ':
    default:
      break;
  }
}

static void draw_char(
    chip_state_t *state,
    int x,
    int y,
    char c,
    int scale,
    uint32_t color)
{
  uint8_t rows[7];

  get_glyph(
      c,
      rows
  );

  for (
      int row = 0;
      row < 7;
      row++)
  {
    for (
        int col = 0;
        col < 5;
        col++)
    {
      if (
          rows[row] &
          (1 << (4 - col)))
      {
        for (
            int sy = 0;
            sy < scale;
            sy++)
        {
          for (
              int sx = 0;
              sx < scale;
              sx++)
          {
            set_pixel(
                state,
                x + col * scale + sx,
                y + row * scale + sy,
                color
            );
          }
        }
      }
    }
  }
}

static void draw_string(
    chip_state_t *state,
    int x,
    int y,
    const char *text,
    int scale,
    uint32_t color)
{
  int cursor =
      x;

  while (*text)
  {
    draw_char(
        state,
        cursor,
        y,
        *text,
        scale,
        color
    );

    cursor +=
        6 * scale;

    text++;
  }
}

static void draw_centered_string(
    chip_state_t *state,
    int y,
    const char *text,
    int scale,
    uint32_t color)
{
  int width =
      (int)strlen(text) *
      6 *
      scale;

  int x =
      (SCREEN_WIDTH - width) /
      2;

  if (x < 0)
  {
    x = 0;
  }

  draw_string(
      state,
      x,
      y,
      text,
      scale,
      color
  );
}

static float mode_maximum(
    uint32_t mode)
{
  switch (mode)
  {
    case 0:
      return 60.0f;

    case 1:
      return 30.0f;

    case 2:
      return 15.0f;

    case 3:
      return 100.0f;

    case 4:
      return 100.0f;

    default:
      return 100.0f;
  }
}

static float clamp_value(
    uint32_t mode,
    float value)
{
  float maximum =
      mode_maximum(mode);

  if (value < 0.0f)
  {
    value = 0.0f;
  }

  if (value > maximum)
  {
    value = maximum;
  }

  return value;
}

static float engineering_value(
    uint32_t mode,
    float position)
{
  return
      position *
      mode_maximum(mode);
}

static float value_to_position(
    uint32_t mode,
    float value)
{
  float maximum =
      mode_maximum(mode);

  value =
      clamp_value(
          mode,
          value
      );

  if (maximum <= 0.0f)
  {
    return 0.0f;
  }

  return
      value /
      maximum;
}

static void format_value(
    uint32_t mode,
    float value,
    char *buffer,
    size_t buffer_size)
{
  switch (mode)
  {
    case 0:
      snprintf(buffer, buffer_size, "%.1fV", value);
      break;

    case 1:
      snprintf(buffer, buffer_size, "%.1fA", value);
      break;

    case 2:
      snprintf(buffer, buffer_size, "%.2fV", value);
      break;

    case 3:
      snprintf(buffer, buffer_size, "%.1fW", value);
      break;

    case 4:
      snprintf(buffer, buffer_size, "%.0f%%", value);
      break;

    default:
      snprintf(buffer, buffer_size, "%.1f", value);
      break;
  }
}

static bool read_manual_value(
    chip_state_t *state,
    float *result)
{
  uint32_t length =
      string_get_length(
          state->manual_value_attr
      );

  if (length == 0)
  {
    return false;
  }

  char buffer[32];

  uint32_t bytes_read =
      string_read(
          state->manual_value_attr,
          buffer,
          sizeof(buffer) - 1
      );

  if (
      bytes_read >=
      sizeof(buffer))
  {
    bytes_read =
        sizeof(buffer) - 1;
  }

  buffer[bytes_read] =
      '\0';

  char *start =
      buffer;

  while (
      *start == ' ' ||
      *start == '\t' ||
      *start == '\r' ||
      *start == '\n')
  {
    start++;
  }

  if (*start == '\0')
  {
    return false;
  }

  char *end_ptr =
      NULL;

  float value =
      strtof(
          start,
          &end_ptr
      );

  if (end_ptr == start)
  {
    return false;
  }

  *result =
      value;

  return true;
}

static const int pointer_x[13] =
{
  -24, -28, -30, -28, -22, -12, 0,
   12,  22,  28,  30,  28, 24
};

static const int pointer_y[13] =
{
   18,  10,   0, -10, -22, -28, -30,
  -28, -22, -10,   0,  10, 18
};

static void draw_standard_scale(
    chip_state_t *state,
    uint32_t mode,
    float position,
    uint32_t text,
    uint32_t tick,
    uint32_t accent)
{
  const int leftX =
      12;

  const int rightX =
      108;

  const int scaleY =
      104;

  draw_line(
      state,
      leftX,
      scaleY,
      rightX,
      scaleY,
      tick
  );

  for (
      int i = 0;
      i <= 10;
      i++)
  {
    int x =
        leftX +
        (
            (rightX - leftX) *
            i
        ) /
        10;

    int tickHeight =
        (
            i == 0 ||
            i == 5 ||
            i == 10
        )
            ? 5
            : 2;

    draw_line(
        state,
        x,
        scaleY - tickHeight,
        x,
        scaleY + 2,
        tick
    );
  }

  int markerX =
      leftX +
      (int)(
          (rightX - leftX) *
          position
      );

  draw_line(
      state,
      markerX,
      scaleY - 7,
      markerX,
      scaleY + 3,
      accent
  );

  switch (mode)
  {
    case 0:
      draw_string(state, 2, 111, "0V", 1, text);
      draw_centered_string(state, 111, "30V", 1, text);
      draw_string(state, 94, 111, "60V", 1, text);
      break;

    case 1:
      draw_string(state, 2, 111, "0A", 1, text);
      draw_centered_string(state, 111, "15A", 1, text);
      draw_string(state, 94, 111, "30A", 1, text);
      break;

    case 2:
      draw_string(state, 2, 111, "0V", 1, text);
      draw_centered_string(state, 111, "7.5V", 1, text);
      draw_string(state, 94, 111, "15V", 1, text);
      break;

    case 3:
      draw_string(state, 2, 111, "0W", 1, text);
      draw_centered_string(state, 111, "50W", 1, text);
      draw_string(state, 88, 111, "100W", 1, text);
      break;
  }
}

static void draw_backhaul_scale(
    chip_state_t *state,
    float position,
    uint32_t text,
    uint32_t tick,
    uint32_t accent)
{
  const int leftX =
      12;

  const int rightX =
      108;

  const int scaleY =
      104;

  draw_line(
      state,
      leftX,
      scaleY,
      rightX,
      scaleY,
      tick
  );

  for (
      int i = 0;
      i <= 4;
      i++)
  {
    int x =
        leftX +
        (
            (rightX - leftX) *
            i
        ) /
        4;

    draw_line(
        state,
        x,
        scaleY - 5,
        x,
        scaleY + 2,
        tick
    );
  }

  int markerX =
      leftX +
      (int)(
          (rightX - leftX) *
          position
      );

  draw_line(
      state,
      markerX,
      scaleY - 7,
      markerX,
      scaleY + 3,
      accent
  );

  draw_string(state, 18, 111, "N", 1, text);
  draw_string(state, 42, 111, "D", 1, text);
  draw_string(state, 66, 111, "C", 1, text);
  draw_string(state, 90, 111, "O", 1, text);
}

static void render_control(
    chip_state_t *state,
    uint32_t mode,
    float value,
    bool manual_mode)
{
  value =
      clamp_value(
          mode,
          value
      );

  float position =
      value_to_position(
          mode,
          value
      );

  uint32_t position_raw =
      (uint32_t)(
          position *
          1000.0f
      );

  char value_string[16];

  format_value(
      mode,
      value,
      value_string,
      sizeof(value_string)
  );

  const uint32_t background =
      rgba(25, 29, 34, 255);

  const uint32_t panel =
      rgba(36, 43, 51, 255);

  const uint32_t knob =
      rgba(195, 201, 208, 255);

  const uint32_t knob_center =
      rgba(70, 78, 87, 255);

  const uint32_t text =
      rgba(240, 244, 248, 255);

  const uint32_t tick =
      rgba(170, 180, 190, 255);

  uint32_t accent =
      rgba(0, 190, 220, 255);

  if (manual_mode)
  {
    accent =
        rgba(175, 90, 255, 255);
  }
  else if (mode == 4)
  {
    if (position < 0.25f)
    {
      accent =
          rgba(50, 205, 120, 255);
    }
    else if (position < 0.50f)
    {
      accent =
          rgba(240, 180, 40, 255);
    }
    else if (position < 0.75f)
    {
      accent =
          rgba(244, 122, 32, 255);
    }
    else
    {
      accent =
          rgba(230, 70, 70, 255);
    }
  }

  clear_display(
      state,
      background
  );

  fill_rect(
      state,
      4,
      4,
      SCREEN_WIDTH - 5,
      28,
      panel
  );

  draw_centered_string(
      state,
      9,
      value_string,
      2,
      text
  );

  // M = keyboard/manual field
  // K = knob/slider
  draw_string(
      state,
      5,
      32,
      manual_mode
          ? "M"
          : "K",
      1,
      accent
  );

  fill_circle(
      state,
      60,
      67,
      33,
      accent
  );

  fill_circle(
      state,
      60,
      67,
      28,
      knob
  );

  fill_circle(
      state,
      60,
      67,
      5,
      knob_center
  );

  int pointerIndex =
      (int)(
          (
              position_raw *
              12U
          ) /
          1000U
      );

  if (pointerIndex < 0)
  {
    pointerIndex = 0;
  }

  if (pointerIndex > 12)
  {
    pointerIndex = 12;
  }

  draw_line(
      state,
      60,
      67,
      60 + pointer_x[pointerIndex],
      67 + pointer_y[pointerIndex],
      knob_center
  );

  if (mode == 4)
  {
    draw_backhaul_scale(
        state,
        position,
        text,
        tick,
        accent
    );
  }
  else
  {
    draw_standard_scale(
        state,
        mode,
        position,
        text,
        tick,
        accent
    );
  }

  buffer_write(
      state->framebuffer,
      0,
      state->pixels,
      sizeof(state->pixels)
  );
}

static void refresh_control(
    void *user_data)
{
  chip_state_t *state =
      (chip_state_t *)
          user_data;

  uint32_t mode =
      attr_read(
          state->mode_attr
      );

  uint32_t position_raw =
      attr_read(
          state->position_attr
      );

  if (position_raw > 1000)
  {
    position_raw =
        1000;
  }

  float knob_position =
      position_raw /
      1000.0f;

  float selected_value =
      engineering_value(
          mode,
          knob_position
      );

  float manual_value =
      0.0f;

  bool manual_mode =
      read_manual_value(
          state,
          &manual_value
      );

  if (manual_mode)
  {
    selected_value =
        clamp_value(
            mode,
            manual_value
        );
  }

  float effective_position =
      value_to_position(
          mode,
          selected_value
      );

  // Wokwi virtual ADC reference is 5V.
  float analog_voltage =
      effective_position *
      5.0f;

  pin_dac_write(
      state->out_pin,
      analog_voltage
  );

  render_control(
      state,
      mode,
      selected_value,
      manual_mode
  );
}

void chip_init(void)
{
  chip_state_t *state =
      calloc(
          1,
          sizeof(chip_state_t)
      );

  state->out_pin =
      pin_init(
          "OUT",
          ANALOG
      );

  state->position_attr =
      attr_init(
          "position",
          500
      );

  state->mode_attr =
      attr_init(
          "mode",
          0
      );

  // Every chip instance gets its own keyboard text field.
  state->manual_value_attr =
      attr_string_init(
          "manualValue"
      );

  state->framebuffer =
      framebuffer_init(
          &state->width,
          &state->height
      );

  const timer_config_t timer_config =
  {
    .callback =
        refresh_control,

    .user_data =
        state,
  };

  state->refresh_timer =
      timer_init(
          &timer_config
      );

  refresh_control(
      state
  );

  timer_start(
      state->refresh_timer,
      50000,
      true
  );
}

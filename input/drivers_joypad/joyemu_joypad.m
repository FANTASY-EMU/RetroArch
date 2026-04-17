/*
 * joyemu_joypad.m
 *
 * Minimal JoyEmu native joypad driver used when JOYENGINE_V2 is disabled.
 * Keeps the historical JoyEmu input path alive without reintroducing
 * any of the old host-audio coupling.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../input_driver.h"
#include "../../tasks/tasks_internal.h"

#ifndef MAX_JOYEMU_AXES
#define MAX_JOYEMU_AXES 6
#endif

static uint32_t joyemu_button_state[MAX_USERS];
static int16_t joyemu_analog_state[MAX_USERS][MAX_JOYEMU_AXES];

void joyemu_input_button_event(unsigned port, unsigned button_id, bool pressed)
{
   if (port >= MAX_USERS || button_id >= 32)
      return;

   if (pressed)
      joyemu_button_state[port] |= (1u << button_id);
   else
      joyemu_button_state[port] &= ~(1u << button_id);
}

void joyemu_input_analog_event(unsigned port, unsigned stick_id, float x_value, float y_value)
{
   int base = 0;

   if (port >= MAX_USERS)
      return;

   if (x_value > 1.0f)
      x_value = 1.0f;
   else if (x_value < -1.0f)
      x_value = -1.0f;

   if (y_value > 1.0f)
      y_value = 1.0f;
   else if (y_value < -1.0f)
      y_value = -1.0f;

   switch (stick_id)
   {
      case RETRO_DEVICE_INDEX_ANALOG_LEFT:
         base = 0;
         break;
      case RETRO_DEVICE_INDEX_ANALOG_RIGHT:
         base = 2;
         break;
      default:
         return;
   }

   joyemu_analog_state[port][base]     = (int16_t)(x_value * 32767.0f);
   joyemu_analog_state[port][base + 1] = (int16_t)(y_value * 32767.0f);
}

void joyemu_input_deinit(void)
{
   memset(joyemu_button_state, 0, sizeof(joyemu_button_state));
   memset(joyemu_analog_state, 0, sizeof(joyemu_analog_state));
}

static void *joyemu_joypad_init(void *data)
{
   unsigned i;
   (void)data;

   joyemu_input_deinit();

   for (i = 0; i < MAX_USERS; i++)
      input_autoconfigure_connect(
            "JoyEmu Controller",
            NULL,
            NULL,
            "joyemu",
            i,
            0,
            0);

   return (void*)-1;
}

static void joyemu_joypad_destroy(void)
{
   joyemu_input_deinit();
}

static void joyemu_joypad_poll(void)
{
}

static bool joyemu_joypad_query_pad(unsigned pad)
{
   return pad < MAX_USERS;
}

static const char *joyemu_joypad_name(unsigned pad)
{
   return (pad < MAX_USERS) ? "JoyEmu Controller" : NULL;
}

static int32_t joyemu_joypad_button(unsigned port, uint16_t joykey)
{
   if (port >= MAX_USERS || joykey >= 32 || GET_HAT_DIR(joykey))
      return 0;

   return (joyemu_button_state[port] & (1u << joykey)) != 0;
}

static void joyemu_joypad_get_buttons(unsigned port, input_bits_t *state)
{
   if (port >= MAX_USERS)
   {
      BIT256_CLEAR_ALL_PTR(state);
      return;
   }

   BITS_COPY16_PTR(state, joyemu_button_state[port]);
}

static int16_t joyemu_joypad_axis(unsigned port, uint32_t joyaxis)
{
   int16_t neg = AXIS_NEG_GET(joyaxis);
   int16_t pos = AXIS_POS_GET(joyaxis);

   if (port >= MAX_USERS)
      return 0;

   if (neg < MAX_JOYEMU_AXES)
   {
      int16_t val = joyemu_analog_state[port][neg];
      if (val < 0)
         return val;
   }

   if (pos < MAX_JOYEMU_AXES)
   {
      int16_t val = joyemu_analog_state[port][pos];
      if (val > 0)
         return val;
   }

   return 0;
}

static int16_t joyemu_joypad_state(
      rarch_joypad_info_t *joypad_info,
      const struct retro_keybind *binds,
      unsigned port)
{
   unsigned i;
   int16_t ret       = 0;
   uint16_t port_idx = joypad_info->joy_idx;

   (void)port;

   if (port_idx >= MAX_USERS)
      return 0;

   for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
   {
      const uint64_t joykey  = (binds[i].joykey != NO_BTN)
         ? binds[i].joykey  : joypad_info->auto_binds[i].joykey;
      const uint32_t joyaxis = (binds[i].joyaxis != AXIS_NONE)
         ? binds[i].joyaxis : joypad_info->auto_binds[i].joyaxis;

      if ((uint16_t)joykey != NO_BTN && !GET_HAT_DIR(joykey) && i < 32)
      {
         if ((joyemu_button_state[port_idx] & (1u << i)) != 0)
            ret |= (1 << i);
      }
      else if (joyaxis != AXIS_NONE &&
            ((float)abs(joyemu_joypad_axis(port_idx, joyaxis)) / 0x8000) > joypad_info->axis_threshold)
         ret |= (1 << i);
   }

   return ret;
}

static bool joyemu_joypad_set_rumble(
      unsigned pad,
      enum retro_rumble_effect type,
      uint16_t strength)
{
   (void)pad;
   (void)type;
   (void)strength;
   return false;
}

input_device_driver_t joyemu_joypad = {
   joyemu_joypad_init,
   joyemu_joypad_query_pad,
   joyemu_joypad_destroy,
   joyemu_joypad_button,
   joyemu_joypad_state,
   joyemu_joypad_get_buttons,
   joyemu_joypad_axis,
   joyemu_joypad_poll,
   joyemu_joypad_set_rumble,
   NULL, /* set_rumble_gain */
   NULL, /* set_sensor_state */
   NULL, /* get_sensor_input */
   joyemu_joypad_name,
   "joyemu",
};

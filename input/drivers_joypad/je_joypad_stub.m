/*  je_joypad_stub.m
 *
 *  Minimal JoyEngine-backed joypad driver for JOYENGINE_V2.
 *  Reads button/analog state written by JoyEngine's JEInputRelay.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../input_driver.h"
#include "../../tasks/tasks_internal.h"

#define JE_MAX_PLAYERS 4
#define JE_MAX_BUTTONS 16
#define JE_MAX_AXES 4

extern int16_t je_input_state[JE_MAX_PLAYERS][JE_MAX_BUTTONS];
extern int16_t je_analog_state[JE_MAX_PLAYERS][2][2];

static void *je_joypad_init(void *data)
{
   unsigned i;
   (void)data;

   for (i = 0; i < JE_MAX_PLAYERS; i++)
      input_autoconfigure_connect(
            "JoyEngine Controller",
            NULL,
            NULL,
            "joyengine",
            i,
            0,
            0);

   return (void*)-1;
}

static void je_joypad_destroy(void)
{
}

static void je_joypad_poll(void)
{
}

static bool je_joypad_query_pad(unsigned pad)
{
   return pad < JE_MAX_PLAYERS;
}

static const char *je_joypad_name(unsigned pad)
{
   return (pad < JE_MAX_PLAYERS) ? "JoyEngine Controller" : NULL;
}

static int32_t je_joypad_button(unsigned port, uint16_t joykey)
{
   if (port >= JE_MAX_PLAYERS || joykey >= JE_MAX_BUTTONS || GET_HAT_DIR(joykey))
      return 0;

   return je_input_state[port][joykey] != 0;
}

static void je_joypad_get_buttons(unsigned port, input_bits_t *state)
{
   uint16_t bits = 0;
   unsigned i    = 0;

   if (port >= JE_MAX_PLAYERS)
   {
      BIT256_CLEAR_ALL_PTR(state);
      return;
   }

   for (i = 0; i < JE_MAX_BUTTONS; i++)
      if (je_input_state[port][i])
         bits |= (1 << i);

   BITS_COPY16_PTR(state, bits);
}

static int16_t je_joypad_axis(unsigned port, uint32_t joyaxis)
{
   int16_t neg = AXIS_NEG_GET(joyaxis);
   int16_t pos = AXIS_POS_GET(joyaxis);

   if (port >= JE_MAX_PLAYERS)
      return 0;

   if (neg < JE_MAX_AXES)
   {
      int16_t val = je_analog_state[port][neg / 2][neg % 2];
      if (val < 0)
         return val;
   }

   if (pos < JE_MAX_AXES)
   {
      int16_t val = je_analog_state[port][pos / 2][pos % 2];
      if (val > 0)
         return val;
   }

   return 0;
}

static int16_t je_joypad_state(
      rarch_joypad_info_t *joypad_info,
      const struct retro_keybind *binds,
      unsigned port)
{
   unsigned i;
   int16_t ret         = 0;
   uint16_t port_idx   = joypad_info->joy_idx;

   (void)port;

   if (port_idx >= JE_MAX_PLAYERS)
      return 0;

   for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
   {
      const uint64_t joykey  = (binds[i].joykey != NO_BTN)
         ? binds[i].joykey  : joypad_info->auto_binds[i].joykey;
      const uint32_t joyaxis = (binds[i].joyaxis != AXIS_NONE)
         ? binds[i].joyaxis : joypad_info->auto_binds[i].joyaxis;

      if ((uint16_t)joykey != NO_BTN && !GET_HAT_DIR(joykey) && i < JE_MAX_BUTTONS)
      {
         if (je_input_state[port_idx][i])
            ret |= (1 << i);
      }
      else if (joyaxis != AXIS_NONE &&
            ((float)abs(je_joypad_axis(port_idx, joyaxis)) / 0x8000) > joypad_info->axis_threshold)
         ret |= (1 << i);
   }

   return ret;
}

input_device_driver_t joyengine_joypad = {
   je_joypad_init,
   je_joypad_query_pad,
   je_joypad_destroy,
   je_joypad_button,
   je_joypad_state,
   je_joypad_get_buttons,
   je_joypad_axis,
   je_joypad_poll,
   NULL, /* set_rumble */
   NULL, /* set_rumble_gain */
   NULL, /* set_sensor_state */
   NULL, /* get_sensor_input */
   je_joypad_name,
   "joyengine",
};

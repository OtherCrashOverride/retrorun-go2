/*
retrorun-go2 - libretro frontend for the ODROID-GO Advance
Copyright (C) 2020  OtherCrashOverride

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "input.h"

#include "globals.h"
#include "video.h"
#include "libretro.h"

#include <go2/input.h>
#include <stdio.h>


extern int opt_backlight;
extern int opt_volume;

bool input_exit_requested = false;
go2_battery_state_t batteryState;

static go2_gamepad_state_t gamepadState;
static go2_gamepad_state_t prevGamepadState;
static go2_input_t* input;


void input_gamepad_read(go2_gamepad_state_t* out_gamepadState)
{
    if (!input)
    {
        input = go2_input_create();
    }

	go2_input_gamepad_read(input, out_gamepadState);
    prevGamepadState = *out_gamepadState;
}

void core_input_poll(void)
{
    if (!input)
    {
        input = go2_input_create();
    }

	go2_input_gamepad_read(input, &gamepadState);
    go2_input_battery_read(input, &batteryState);

    if (!prevGamepadState.buttons.f1 && gamepadState.buttons.f1)
    {
        input_exit_requested = true;
    }

    if (!prevGamepadState.buttons.f2 && gamepadState.buttons.f2)
    {
        screenshot_requested = true;
    }

    if (gamepadState.buttons.f4)
    {
        if (gamepadState.dpad.up && !prevGamepadState.dpad.up)
        {
            opt_backlight += 10;
            if (opt_backlight > 100) opt_backlight = 100;
            
            printf("Backlight+ = %d\n", opt_backlight);
        }
        else if (gamepadState.dpad.down && !prevGamepadState.dpad.down)
        {
            opt_backlight -= 10;
            if (opt_backlight < 1) opt_backlight = 1;

            printf("Backlight- = %d\n", opt_backlight);
        }

        if (gamepadState.dpad.right && !prevGamepadState.dpad.right)
        {
            opt_volume += 5;
            if (opt_volume > 100) opt_volume = 100;

            printf("Volume+ = %d\n", opt_volume);
        }
        else if (gamepadState.dpad.left && !prevGamepadState.dpad.left)
        {
            opt_volume -= 5;
            if (opt_volume < 0) opt_volume = 0;

            printf("Volume- = %d\n", opt_volume);
        }
    }

    prevGamepadState = gamepadState;
}

int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    //int16_t result;

    // if (port || index || device != RETRO_DEVICE_JOYPAD)
    //         return 0;

    if (!Retrorun_UseAnalogStick)
    {
        // Map thumbstick to dpad
        const float TRIM = 0.35f;
        
        if (gamepadState.thumb.y < -TRIM) gamepadState.dpad.up = ButtonState_Pressed;
        if (gamepadState.thumb.y > TRIM) gamepadState.dpad.down = ButtonState_Pressed;
        if (gamepadState.thumb.x < -TRIM) gamepadState.dpad.left = ButtonState_Pressed;
        if (gamepadState.thumb.x > TRIM) gamepadState.dpad.right = ButtonState_Pressed;
    }

/*
#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L       10
#define RETRO_DEVICE_ID_JOYPAD_R       11
#define RETRO_DEVICE_ID_JOYPAD_L2      12
#define RETRO_DEVICE_ID_JOYPAD_R2      13
#define RETRO_DEVICE_ID_JOYPAD_L3      14
#define RETRO_DEVICE_ID_JOYPAD_R3      15
*/

    if (port == 0)
    {
        if (device == RETRO_DEVICE_JOYPAD)
        {
            switch (id)
            {
                case RETRO_DEVICE_ID_JOYPAD_B:
                    return gamepadState.buttons.b;
                    break;
                
                case RETRO_DEVICE_ID_JOYPAD_Y:
                    return gamepadState.buttons.y;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_SELECT:
                    return gamepadState.buttons.f3;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_START:
                    return gamepadState.buttons.f4;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_UP:
                    return gamepadState.dpad.up;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_DOWN:
                    return gamepadState.dpad.down;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_LEFT:
                    return gamepadState.dpad.left;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_RIGHT:
                    return gamepadState.dpad.right;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_A:
                    return gamepadState.buttons.a;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_X:
                    return gamepadState.buttons.x;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_L:
                    return opt_triggers ? gamepadState.buttons.f5 : gamepadState.buttons.top_left;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_R:
                    return opt_triggers ? gamepadState.buttons.f6 : gamepadState.buttons.top_right;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_L2:
                    return opt_triggers ? gamepadState.buttons.top_left : gamepadState.buttons.f5;
                    break;

                case RETRO_DEVICE_ID_JOYPAD_R2:
                    return opt_triggers ? gamepadState.buttons.top_right : gamepadState.buttons.f6;
                    break;

                default:
                    return 0;
                    break;
            }
        }
        else if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
        {
            if (gamepadState.thumb.x > 1.0f)
                gamepadState.thumb.x = 1.0f;
            else if (gamepadState.thumb.x < -1.0f)
                gamepadState.thumb.x = -1.0f;
            
            if (gamepadState.thumb.y > 1.0f)
                gamepadState.thumb.y = 1.0f;
            else if (gamepadState.thumb.y < -1.0f)
                gamepadState.thumb.y = -1.0f;

            switch (id)
            {
                case RETRO_DEVICE_ID_ANALOG_X:
                    return gamepadState.thumb.x * 0x7fff;
                    break;
                
                case RETRO_DEVICE_ID_JOYPAD_Y:
                    return gamepadState.thumb.y * 0x7fff;
                    break;
                    
                default:
                    return 0;
                    break;
            }
        }
        
    }

    return 0;
}

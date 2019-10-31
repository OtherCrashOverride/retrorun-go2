#pragma once

#include <stdint.h>
#include <stdbool.h>


extern bool input_exit_requested;


void core_input_poll(void);
int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id);

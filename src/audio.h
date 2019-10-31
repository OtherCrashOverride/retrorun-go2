#pragma once

#include <stdlib.h>

void audio_init(int freq);
void audio_deinit();
void core_audio_sample(int16_t left, int16_t right);
size_t core_audio_sample_batch(const int16_t * data, size_t frames);

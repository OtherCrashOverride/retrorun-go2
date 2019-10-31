#include "audio.h"

#include <stdio.h>

#include <go2/audio.h>


static go2_audio_t* audio;


void audio_init(int freq)
{
    // Note: audio stutters in OpenAL unless the buffer frequency at upload
    // is the same as during creation.
    audio = go2_audio_create(freq);
    //printf("audio_init: freq=%d\n", freq);
}

void audio_deinit()
{

}

void core_audio_sample(int16_t left, int16_t right)
{
	// (void)left;
	// (void)right;
}

size_t core_audio_sample_batch(const int16_t * data, size_t frames)
{
	go2_audio_submit(audio, (const short*)data, frames);
	return 0;
}

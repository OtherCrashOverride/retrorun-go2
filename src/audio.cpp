#include "audio.h"

#include <stdio.h>
#include <string.h>

#include <go2/audio.h>

#define FRAMES_MAX (48000)
#define CHANNELS (2)

extern int opt_volume;


static go2_audio_t* audio;
static u_int16_t audioBuffer[FRAMES_MAX * CHANNELS];
static int audioFrameCount;
static int audioFrameLimit;

void audio_init(int freq)
{
    // Note: audio stutters in OpenAL unless the buffer frequency at upload
    // is the same as during creation.
    audio = go2_audio_create(freq);
    audioFrameCount = 0;
    audioFrameLimit = 1.0 / 60.0 * freq;

    //printf("audio_init: freq=%d\n", freq);

    if (opt_volume > -1)
    {
        go2_audio_volume_set(audio, (uint32_t) opt_volume);
    }
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
    if (audioFrameCount + frames > audioFrameLimit)
    {
        go2_audio_submit(audio, (const short*)audioBuffer, audioFrameCount);
        audioFrameCount = 0;
    }

    memcpy(audioBuffer + (audioFrameCount * CHANNELS), data, frames * sizeof(int16_t) * CHANNELS);
    audioFrameCount += frames;

	return 0;
}

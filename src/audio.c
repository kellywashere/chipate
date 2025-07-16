#include <raylib.h>
#include <math.h>
#include "audio.h"

const float audio_freq = 1000.0f;
const int   audio_bufsize = 4096;
const float audio_volume = 0.4f; // 0..1 volume of beep

// see: https://github.com/raysan5/raylib/blob/master/examples/audio/audio_raw_stream.c

// global so we do not have to pass to user
AudioStream audio_stream;

// global: audio phase and on/off (used in callback)
float audio_phase = 0.0f;
bool  audio_beep = false; // when true: beep is on

static void audio_callback(void *buffer, unsigned int frames) {
	float phase_incr = 2.0f * PI * audio_freq / 44100.0f;
	short *d = (short *)buffer;

	for (unsigned int ii = 0; ii < frames; ii++) {
		short sample = (short)(audio_volume * 32760.0f * sinf(audio_phase));
		d[ii] = audio_beep ? sample : 0;
		audio_phase += phase_incr;
		if (audio_phase > 2.0f * PI)
			audio_phase -= 2.0f * PI;
	}
}

void audio_set_beep(bool beep_on) {
	if (!audio_beep && beep_on)
		audio_phase = 0.0f;
	audio_beep = beep_on;
}

void audio_init() {
	InitAudioDevice();
	SetAudioStreamBufferSizeDefault(audio_bufsize);

	// Init raw audio stream (sample rate: 44100, sample size: 16bit-short, channels: 1-mono)
	audio_stream = LoadAudioStream(44100, 16, 1);
	SetAudioStreamCallback(audio_stream, audio_callback);
	audio_beep = false; // just to be sure...
	PlayAudioStream(audio_stream);
}

void audio_exit() {
	UnloadAudioStream(audio_stream);
	CloseAudioDevice();
}

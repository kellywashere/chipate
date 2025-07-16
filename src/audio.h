#ifndef __AUDIO_H__
#define __AUDIO_H__

#include <stdbool.h>
#include <raylib.h>

void audio_init();
void audio_exit();
void audio_set_beep(bool beep_on);

#endif

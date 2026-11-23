/*
 * audio.h - Procedural sound effects and dynamic music for Guard Escape.
 */
#ifndef GE_AUDIO_H
#define GE_AUDIO_H

void audio_init(void);
void audio_update(float dt);
void audio_shutdown(void);

/* Sound effect triggers */
void audio_play_footstep(void);
void audio_play_sprint_step(void);
void audio_play_coin(void);
void audio_play_locker(void);
void audio_play_spotlight_alert(void);
void audio_play_exhausted(void);
void audio_play_key(void);
void audio_play_all_keys(void);
void audio_play_alert(void);
void audio_play_caught(void);
void audio_play_level_clear(void);
void audio_play_menu_click(void);
void audio_play_dart_fire(void);
void audio_play_dart_hit(void);
void audio_play_flash(void);
void audio_play_pickup(void);

#endif

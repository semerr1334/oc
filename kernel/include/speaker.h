/*
 * OC OS — динамик ПК (PC speaker): звуки и мелодии.
 */
#ifndef OC_SPEAKER_H
#define OC_SPEAKER_H

#include "types.h"

void speaker_tone(u32 hz, u32 ms);  /* 0 Гц = пауза */
void speaker_beep(void);            /* писк 880 Гц */
void speaker_freq(u32 hz);          /* непрерывный тон, 0 = выключить */
void speaker_off(void);             /* динамик выключить */
void speaker_jingle(void);          /* стартовая мелодия */
void speaker_song(void);            /* мотив «до-ми-соль-до» */

#endif /* OC_SPEAKER_H */

/*
 * OC OS — динамик ПК (8253 PIT, канал 2 + порт 0x61).
 *
 * Динамик есть во всех ПК и ноутбуках (пусть и через SMM) — система
 * теперь звучит: писк, мелодии, сигналы меню.
 */
#include "speaker.h"
#include "io.h"
#include "pit.h"

#define PIT_BASE 1193182u

static void speaker_gate(bool on)
{
    u8 v = inb(0x61);
    if (on)
        outb(0x61, v | 0x03);       /* динамик + вывод канала 2 */
    else
        outb(0x61, v & ~0x03);
}

void speaker_tone(u32 hz, u32 ms)
{
    if (!hz) {                      /* пауза */
        speaker_gate(false);
        pit_sleep_ms(ms);
        return;
    }
    u32 div = PIT_BASE / hz;
    outb(0x43, 0xB6);               /* канал 2, режим 3 (меандр) */
    outb(0x42, (u8)div);
    outb(0x42, (u8)(div >> 8));
    speaker_gate(true);
    pit_sleep_ms(ms);
    speaker_gate(false);
}

void speaker_beep(void)
{
    speaker_tone(880, 150);
}

/* непрерывный тон без ожидания (пианино, игры) */
void speaker_freq(u32 hz)
{
    if (!hz) {
        speaker_gate(false);
        return;
    }
    u32 div = PIT_BASE / hz;
    outb(0x43, 0xB6);               /* канал 2, режим 3 (меандр) */
    outb(0x42, (u8)div);
    outb(0x42, (u8)(div >> 8));
    speaker_gate(true);
}

void speaker_off(void)
{
    speaker_gate(false);
}

/* до(262) ми(330) соль(392) до(523) */
static const struct { u32 hz; u32 ms; } melody[] = {
    { 262, 110 }, { 330, 110 }, { 392, 110 }, { 523, 180 }, { 0, 60 },
};

void speaker_jingle(void)
{
    for (u32 i = 0; i < sizeof(melody) / sizeof(melody[0]); i++)
        speaker_tone(melody[i].hz, melody[i].ms);
}

void speaker_song(void)
{
    for (int rep = 0; rep < 2; rep++)
        for (u32 i = 0; i < sizeof(melody) / sizeof(melody[0]); i++)
            speaker_tone(melody[i].hz, melody[i].ms);
}

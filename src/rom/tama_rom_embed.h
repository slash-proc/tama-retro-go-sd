#pragma once
#include <stdint.h>
#include <stddef.h>

/* Pre-decoded Tamagotchi P1 program (u12_t words). */
#define TAMA_ROM_WORD_COUNT 6144
extern const uint16_t tama_rom_embed[TAMA_ROM_WORD_COUNT];

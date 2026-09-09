/*
 * Tamagotchi P1 — GWHB homebrew (embedded ROM via TamaLIB).
 *
 * Port of Core/Src/porting/tama from game-and-watch-retro-go-sd.
 * Build: make PROJECT_KIND=homebrew && make host PROJECT_KIND=homebrew
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "rom_manager.h"
#include "odroid_system.h"
#include "appid.h"
#include "odroid_overlay.h"
#include "rg_rtc.h"

#include "icons_tama.h"
#include "state_tama.h"
#include "tamalib.h"
#include "rom/tama_rom_embed.h"
#include "tama_i18n.h"

#ifndef HOST_BUILD
#include "gw_core_bridge.h"
#include "gw_core_i18n.h"
#else
#include "host_compat.h"
#include "gw_core_i18n.h"
#endif

#define APP_ID APPID_HOMEBREW

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define ODROID_DIALOG_CHOICE_SEPARATOR {0x0F0F0F0E, "-", "-", -1, NULL}

/**
 * Tamagotchi P1 emulator (Epson S1C6S46).
 * Datasheet: https://download.epson-europe.com/pub/electronics-de/asmic/4bit/62family/technicalmanual/tm_6s46.pdf
 */

#define TAMA_CLOCK_RATE 32768
#define TAMA_SAMPLE_RATE TAMA_CLOCK_RATE
#define TAMA_FRAME_RATE 60
#define TAMA_LCD_FPS TAMA_FRAME_RATE
#define TAMA_CLOCKS_PER_FRAME (TAMA_CLOCK_RATE / TAMA_FRAME_RATE)
#define TAMA_LCD_WIDTH LCD_WIDTH
#define TAMA_LCD_HEIGHT LCD_HEIGHT
#define TAMA_MAX_ICON_NUM ICON_NUM
#define TAMA_ICON_HEIGHT 32
#define TAMA_ICON_WIDTH 32
#define TAMA_ICONS_PER_ROW 4
#define TAMA_LOG_ENABLED false
#define TAMA_ROM_SIZE_MAX TAMA_ROM_WORD_COUNT
#define MAX_SAVE_AGE_IN_MILLIS (2 * 24 * 60 * 60 * 1000)

#define TAMA_DBG_FG 0xD347
#define TAMA_DBG_BG 0x6000

static u12_t tama_rom[TAMA_ROM_SIZE_MAX] __attribute__((aligned(4)));
static bool_t tama_lcd[LCD_WIDTH][LCD_HEIGHT] __attribute__((aligned(4)));
static bool_t tama_lcd_icons[TAMA_MAX_ICON_NUM] __attribute__((aligned(4)));
static int8_t tama_audio[TAMA_CLOCKS_PER_FRAME] __attribute__((aligned(4)));
static uint8_t tama_sound_period = 0;

static void blit(void);

#define SAVE_STATE_BUFFER_SIZE 4192
static uint8_t save_buffer[SAVE_STATE_BUFFER_SIZE];
static int8_t current_save_slot;
static bool_t current_load_state;
static char readSavePathName[256];

static state_t *state;
static volatile bool reload;

static uint8_t current_period;
static uint8_t current_half_period;
static int16_t sample_high;
static int16_t sample_low;
static uint8_t period_index;
static u64_t frame_start_tick_counter = 0;

static unsigned int emu_cycles, blit_cycles, audio_cycles, loop_cycles;

static void tama_display_write_rect(short left, short top, short width, short height, short stride, const uint16_t *buffer)
{
    pixel_t *dest = lcd_get_active_buffer();

    for (short y = 0; y < height; y++) {
        if ((y + top) >= GW_LCD_HEIGHT)
            return;
        pixel_t *dest_row = &dest[(y + top) * GW_LCD_WIDTH + left];
        memcpy(dest_row, &buffer[y * stride], width * sizeof(pixel_t));
    }
}

static bool debug_display_ram = false;
static bool debug_display_ram_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    (void)repeat;
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT)
        debug_display_ram = !debug_display_ram;

    strcpy(option->value, debug_display_ram ? gw_i18n(tama_i18n_yes) : gw_i18n(tama_i18n_no));

    return event == ODROID_DIALOG_ENTER;
}

static void display_ram_overlay(void)
{
    char draw_line_content[80];
    sprintf(draw_line_content, "   00000000000000001111111111111111");
    odroid_overlay_draw_text(10, 32, 300, draw_line_content, TAMA_DBG_FG, TAMA_DBG_BG);
    sprintf(draw_line_content, "   0123456789ABCDEF0123456789ABCDEF");
    odroid_overlay_draw_text(10, 40, 300, draw_line_content, TAMA_DBG_FG, TAMA_DBG_BG);
    for (unsigned char i = 0; i < (MEM_RAM_SIZE / 32); i++) {
        sprintf(draw_line_content, "%2X %1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X%1X", i,
                state->memory[(i * 16) + 0], state->memory[(i * 16) + 1], state->memory[(i * 16) + 2], state->memory[(i * 16) + 3],
                state->memory[(i * 16) + 4], state->memory[(i * 16) + 5], state->memory[(i * 16) + 6], state->memory[(i * 16) + 7],
                state->memory[(i * 16) + 8], state->memory[(i * 16) + 9], state->memory[(i * 16) + 10], state->memory[(i * 16) + 11],
                state->memory[(i * 16) + 12], state->memory[(i * 16) + 13], state->memory[(i * 16) + 14], state->memory[(i * 16) + 15],
                state->memory[(i * 16) + 16], state->memory[(i * 16) + 17], state->memory[(i * 16) + 18], state->memory[(i * 16) + 19],
                state->memory[(i * 16) + 20], state->memory[(i * 16) + 21], state->memory[(i * 16) + 22], state->memory[(i * 16) + 23],
                state->memory[(i * 16) + 24], state->memory[(i * 16) + 25], state->memory[(i * 16) + 26], state->memory[(i * 16) + 27],
                state->memory[(i * 16) + 28], state->memory[(i * 16) + 29], state->memory[(i * 16) + 30], state->memory[(i * 16) + 31]);
        odroid_overlay_draw_text(10, 48 + 8 * i, 300, draw_line_content, TAMA_DBG_FG, TAMA_DBG_BG);
    }
}

static void load_rom(void)
{
    memcpy(tama_rom, tama_rom_embed, sizeof(tama_rom));
}

static bool LoadStateFromFile(void)
{
    printf("LoadState: loading %s...\n", readSavePathName);
    FILE *file = fopen(readSavePathName, "rb");
    if (file != NULL) {
        fread(save_buffer, 1, SAVE_STATE_BUFFER_SIZE, file);
        fclose(file);
        if (!tama_state_load(save_buffer)) {
            printf("LoadState: failed.\n");
            return false;
        }
        tamalib_refresh_hw();
        printf("LoadState: done.\n");
    } else {
        printf("LoadState: file not found.\n");
        return false;
    }
    return true;
}

static bool LoadState(const char *savePathName)
{
    printf("Loading : %s\n", savePathName);
    strcpy(readSavePathName, savePathName);
    current_load_state = true;
    reload = true;
    return true;
}

static bool SaveState(const char *savePathName)
{
    printf("SaveState: saving %s ...\n", savePathName);
    bool result = tama_state_save(save_buffer);
    if (result) {
        FILE *file = fopen(savePathName, "wb");
        if (file != NULL) {
            fwrite(save_buffer, 1, SAVE_STATE_BUFFER_SIZE, file);
            fclose(file);
            printf("SaveState: done.\n");
        } else {
            printf("SaveState: failed to open file.\n");
        }
    } else {
        printf("SaveState: failed.\n");
    }
    return result;
}

static void *Screenshot(void)
{
    lcd_wait_for_vblank();
    blit();
    return lcd_get_active_buffer();
}

static bool_t hal_is_log_enabled(log_level_t level)
{
    (void)level;
    return TAMA_LOG_ENABLED;
}

static void hal_log(log_level_t level, char *buff, ...)
{
    (void)level;
    va_list args;
    va_start(args, buff);
    vprintf(buff, args);
    va_end(args);
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, uint8_t val)
{
    tama_lcd[x][y] = val == 0 ? 0 : val == 1 ? 0xFF : val;
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
    tama_lcd_icons[icon] = val;
}

static void hal_set_sound_period(uint8_t period)
{
    tama_sound_period = period;
}

static void hal_play_sound(bool_t en)
{
    u64_t index = *state->tick_counter - frame_start_tick_counter;
    if (index < sizeof(tama_audio)) {
        tama_audio[index] = en ? tama_sound_period : 0;
    }
}

static void hal_halt(void)
{
}

static hal_t hal = {
    .halt = &hal_halt,
    .is_log_enabled = &hal_is_log_enabled,
    .log = &hal_log,
    .set_lcd_matrix = &hal_set_lcd_matrix,
    .set_lcd_icon = &hal_set_lcd_icon,
    .set_sound_period = &hal_set_sound_period,
    .play_sound = &hal_play_sound,
};

static void blit(void)
{
    const uint16_t factor = 10;
    const uint16_t matrix_start_y = GW_LCD_HEIGHT / 2 - (TAMA_LCD_HEIGHT * factor / 2);

    uint16_t *screen = lcd_clear_active_buffer();
    for (uint16_t x = 0; x < TAMA_LCD_WIDTH; x++) {
        for (uint16_t y = 0; y < TAMA_LCD_HEIGHT; y++) {
            uint8_t pixel = tama_lcd[x][y];
            for (uint16_t f = 0; f < factor - 2; f++) {
                memset(&screen[(((y * factor + f) + matrix_start_y) * GW_LCD_WIDTH) + (x * factor) + 1], pixel, (factor - 2) * sizeof(uint16_t));
            }
        }
    }

    const uint16_t icon_start_x = (GW_LCD_WIDTH - (TAMA_ICONS_PER_ROW * TAMA_ICON_WIDTH)) / (TAMA_ICONS_PER_ROW + 1);
    const uint16_t icon_space_x = icon_start_x + TAMA_ICON_WIDTH;
    const uint16_t icon_start_y_top = (matrix_start_y / 2) - (TAMA_ICON_HEIGHT / 2);
    const uint16_t icon_start_y_bottom = GW_LCD_HEIGHT - icon_start_y_top - TAMA_ICON_HEIGHT;

    for (uint16_t i = 0; i < TAMA_ICONS_PER_ROW; i++) {
        if (tama_lcd_icons[i]) {
            tama_display_write_rect(icon_start_x + (i * icon_space_x), icon_start_y_top, TAMA_ICON_HEIGHT, TAMA_ICON_WIDTH, TAMA_ICON_WIDTH, tama_icons[i]);
        }
        if (tama_lcd_icons[i + TAMA_ICONS_PER_ROW]) {
            tama_display_write_rect(icon_start_x + (i * icon_space_x), icon_start_y_bottom, TAMA_ICON_HEIGHT, TAMA_ICON_WIDTH, TAMA_ICON_WIDTH, tama_icons[i + TAMA_ICONS_PER_ROW]);
        }
    }

    common_ingame_overlay();
}

static void init_audio_generator(void)
{
    current_period = 0;
    current_half_period = 0;
    sample_high = 0;
    sample_low = 0;
    period_index = 0;
}

static void submit_audio(void)
{
    if (common_emu_sound_loop_is_muted()) {
        memset(tama_audio, -1, sizeof(tama_audio));
        return;
    }

    const int16_t factor = common_emu_sound_get_volume();
    int16_t *sound_buffer = audio_get_active_buffer();
    const uint16_t sound_buffer_length = audio_get_buffer_length();

    audio_clear_active_buffer();

    for (int i = 0; i < sound_buffer_length; i++) {
        int8_t next_period = tama_audio[i];
        if (next_period >= 0 && current_period != next_period) {
            current_period = next_period;
            current_half_period = current_period >> 1;
            period_index = 0;
            sample_high = factor * SHRT_MAX >> 10;
            sample_low = factor * SHRT_MIN >> 10;
        }

        if (current_period > 0) {
            sound_buffer[i] = period_index < current_half_period ? sample_high : sample_low;

            if (++period_index >= current_period) {
                period_index = 0;
            }
        }
    }

    memset(tama_audio, -1, sizeof(tama_audio));
}

static void update_buttons(odroid_gamepad_state_t *joystick)
{
    tamalib_set_button(BTN_LEFT, joystick->values[ODROID_INPUT_LEFT] || joystick->values[ODROID_INPUT_RIGHT] || joystick->values[ODROID_INPUT_UP] || joystick->values[ODROID_INPUT_DOWN] ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    tamalib_set_button(BTN_MIDDLE, joystick->values[ODROID_INPUT_B] || joystick->values[ODROID_INPUT_Y] ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    tamalib_set_button(BTN_RIGHT, joystick->values[ODROID_INPUT_A] || joystick->values[ODROID_INPUT_X] ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
}

/* Embedded P1 ROM — sync host wall clock into known memory offsets. */
static void update_clock(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    set_memory(0x10, tm->tm_sec % 10);
    set_memory(0x11, tm->tm_sec / 10);
    set_memory(0x12, tm->tm_min % 10);
    set_memory(0x13, tm->tm_min / 10);
    set_memory(0x14, tm->tm_hour & 0x0F);
    set_memory(0x15, tm->tm_hour >> 4);
}

static void end_fast_forward(bool *fast_forward_ptr)
{
    *fast_forward_ptr = false;
    common_emu_frame_loop_reset();
    update_clock();
}

static void emulate_next_frame(bool *fast_forward_ptr, u64_t *total_fast_forward_clocks_ptr)
{
    frame_start_tick_counter = *state->tick_counter;
    uint64_t target = 0;
    if (*fast_forward_ptr) {
        uint64_t target_fast_forward_clocks = (GW_GetCurrentMillis() - *state->save_time) * TAMA_CLOCK_RATE / 1000;
        if (*total_fast_forward_clocks_ptr < target_fast_forward_clocks) {
            uint64_t delta = MIN(target_fast_forward_clocks - *total_fast_forward_clocks_ptr, TAMA_CLOCKS_PER_FRAME * TAMA_FRAME_RATE * 50);
            if (delta > TAMA_CLOCKS_PER_FRAME) {
                *total_fast_forward_clocks_ptr += delta;
                target = *state->tick_counter + delta;
            } else {
                end_fast_forward(fast_forward_ptr);
            }
        } else {
            end_fast_forward(fast_forward_ptr);
        }
    }

    if (!*fast_forward_ptr) {
        target = *state->tick_counter + TAMA_CLOCKS_PER_FRAME;
    }

    while (*state->tick_counter < target) {
        tamalib_step();
    }

    if (*fast_forward_ptr && (GW_GetCurrentSubSeconds() > 127)) {
        for (uint8_t i = 0; i < 2; i++) {
            hal_set_lcd_matrix(1 + (i * 4), 0, 0x11);
            hal_set_lcd_matrix(2 + (i * 4), 0, 0x11);
            hal_set_lcd_matrix(3 + (i * 4), 0, 0x11);
            hal_set_lcd_matrix(1 + (i * 4), 1, 0x11);
            hal_set_lcd_matrix(1 + (i * 4), 2, 0x11);
            hal_set_lcd_matrix(2 + (i * 4), 2, 0x11);
            hal_set_lcd_matrix(1 + (i * 4), 3, 0x11);
            hal_set_lcd_matrix(1 + (i * 4), 4, 0x11);
            hal_set_lcd_matrix(3 + (i * 4), 4, 0x11);
        }
    }
}

static void main_tama(uint8_t start_paused)
{
    odroid_gamepad_state_t joystick;

    memset(tama_rom, 0, sizeof(tama_rom));
    memset(tama_lcd, 0, sizeof(tama_lcd));
    memset(tama_lcd_icons, 0, sizeof(tama_lcd_icons));
    memset(tama_audio, -1, sizeof(tama_audio));

    lcd_set_refresh_rate(TAMA_LCD_FPS);

    common_emu_state.frame_time_10us = (uint16_t)(100000 / TAMA_FRAME_RATE + 0.5f);

    load_rom();

    tamalib_register_hal(&hal);
    tamalib_init(tama_rom, TAMA_CLOCK_RATE);
    state = tamalib_get_state();

    if (current_load_state) {
        LoadStateFromFile();
    }

    char debug_display_ram_text[16];
    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {100, gw_i18n(tama_i18n_display_ram), debug_display_ram_text, 1, &debug_display_ram_cb},
        ODROID_DIALOG_CHOICE_LAST};

    init_audio_generator();
    audio_start_playing(TAMA_SAMPLE_RATE / TAMA_FRAME_RATE);

    if (start_paused) {
        common_emu_state.pause_after_frames = 1;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    u64_t total_fast_forward_clocks = 0;
    bool fast_forward = false;
    uint64_t currentMillis = GW_GetCurrentMillis();
    if (*state->save_time != 0 && currentMillis > *state->save_time && (currentMillis - *state->save_time) < MAX_SAVE_AGE_IN_MILLIS) {
        fast_forward = true;
    } else {
        update_clock();
    }

    reload = false;
    while (!reload) {
        common_emu_clear_dwt_cycles();

        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();
        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);

        if (!fast_forward) {
            update_buttons(&joystick);
        }

        emulate_next_frame(&fast_forward, &total_fast_forward_clocks);

        emu_cycles = common_emu_get_dwt_cycles();

        if (drawFrame || fast_forward) {
            blit();
            if (debug_display_ram) {
                display_ram_overlay();
            }
            lcd_swap();
        }

        blit_cycles = common_emu_get_dwt_cycles();

        if (!fast_forward) {
            submit_audio();
        }

        audio_cycles = common_emu_get_dwt_cycles();

        if (!fast_forward) {
            common_emu_sound_sync(false);
        }

        loop_cycles = common_emu_get_dwt_cycles();
        (void)emu_cycles;
        (void)blit_cycles;
        (void)audio_cycles;
        (void)loop_cycles;
    }

    audio_stop_playing();
}

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_system_init(APP_ID, TAMA_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, NULL, NULL);

    if (load_state) {
        current_save_slot = save_slot;
        printf("load state %d\n", save_slot);
        odroid_system_emu_load_state(save_slot);

        current_load_state = true;
    } else {
        lcd_clear_buffers();
        current_save_slot = 0;
        current_load_state = false;
    }

    while (true) {
        main_tama(start_paused);
        start_paused = false;
    }
}

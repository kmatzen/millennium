#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../plugins.h"
#include "../logger.h"
#include "../millennium_sdk.h"

/* Jukebox plugin data */
typedef struct {
    int inserted_cents;
    int song_cost_cents;
    int selected_song;
    int is_playing;
    time_t last_activity;
    time_t play_start_time;
    int play_duration_seconds;
} jukebox_data_t;

static jukebox_data_t jukebox_data = {0};

/* External references */
extern daemon_state_data_t *daemon_state;
extern millennium_client_t *client;

/* Song database */
typedef struct {
    const char *title;
    const char *artist;
    int duration_seconds;
} song_info_t;

static const song_info_t songs[] = {
    {"Bohemian Rhapsody", "Queen", 355},
    {"Hotel California", "Eagles", 391},
    {"Stairway to Heaven", "Led Zeppelin", 482},
    {"Sweet Child O Mine", "Guns N Roses", 356},
    {"Imagine", "John Lennon", 183},
    {"Billie Jean", "Michael Jackson", 294},
    {"Like a Rolling Stone", "Bob Dylan", 366},
    {"Smells Like Teen Spirit", "Nirvana", 301},
    {"What's Going On", "Marvin Gaye", 233}
};

#define NUM_SONGS (sizeof(songs) / sizeof(songs[0]))

/* Internal functions */
static void jukebox_show_welcome(void);
static void jukebox_show_menu(void);
static void jukebox_show_playing(void);
static void jukebox_play_song(int song_number);
static void jukebox_stop_song(void);
static void jukebox_check_playback(void);

/* Jukebox event handlers */
static int jukebox_handle_coin(int coin_value, const char *coin_code) {
    if (coin_value > 0) {
        jukebox_data.inserted_cents += coin_value;
        jukebox_data.last_activity = time(NULL);
        
        if (jukebox_data.inserted_cents >= jukebox_data.song_cost_cents) {
            jukebox_show_menu();
        } else {
            jukebox_show_welcome();
        }
        
        logger_infof_with_category("Jukebox", 
                "Coin inserted: %s, value: %d cents, total: %d cents",
                coin_code, coin_value, jukebox_data.inserted_cents);
    }
    return 0;
}

static int jukebox_handle_keypad(char key) {
    if (jukebox_data.is_playing) {
        /* Handle playback controls */
        if (key == '*') {
            /* Stop current song */
            jukebox_stop_song();
        } else if (key == '#') {
            /* Show menu */
            jukebox_show_menu();
        }
        return 0;
    }
    
    /* Handle song selection */
    if (key >= '1' && key <= '9') {
        int song_number = key - '1'; /* Convert to 0-8 */
        if (song_number < (int)NUM_SONGS) {
            if (jukebox_data.inserted_cents >= jukebox_data.song_cost_cents) {
                jukebox_data.inserted_cents -= jukebox_data.song_cost_cents;
                jukebox_play_song(song_number);
            }
        }
    }
    return 0;
}

static int jukebox_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        /* Reset for new session */
        jukebox_data.inserted_cents = 0;
        jukebox_data.selected_song = -1;
        jukebox_data.is_playing = 0;
        jukebox_data.last_activity = time(NULL);
        jukebox_show_welcome();
    } else if (hook_down) {
        /* Stop playback and return coins */
        if (jukebox_data.is_playing) {
            jukebox_stop_song();
        }
        if (jukebox_data.inserted_cents > 0) {
            jukebox_data.inserted_cents = 0;
        }
        jukebox_data.selected_song = -1;
        jukebox_data.is_playing = 0;
        jukebox_show_welcome();
    }
    return 0;
}

static int jukebox_handle_call_state(int call_state) {
    /* Jukebox doesn't handle calls */
    (void)call_state; /* Suppress unused parameter warning */
    return 0;
}

/* Internal function implementations */
static void jukebox_show_welcome(void) {
    char line2[21];
    sprintf(line2, "Insert %d cents", jukebox_data.song_cost_cents);
    
    char display_bytes[100];
    size_t pos = 0;
    int i;
    
    /* Add line1, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
        display_bytes[pos++] = (i < 7) ? "JUKEBOX"[i] : ' ';
    }
    
    /* Add line feed */
    display_bytes[pos++] = 0x0A;
    
    /* Add line2, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 1; i++) {
        display_bytes[pos++] = (i < (int)strlen(line2)) ? line2[i] : ' ';
    }
    
    /* Null terminate */
    display_bytes[pos] = '\0';
    
    millennium_client_set_display(client, display_bytes);
}

static void jukebox_show_menu(void) {
    char display_bytes[100];
    size_t pos = 0;
    int i;
    
    /* Add line1, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
        display_bytes[pos++] = (i < 10) ? "Select Song"[i] : ' ';
    }
    
    /* Add line feed */
    display_bytes[pos++] = 0x0A;
    
    /* Add line2, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 1; i++) {
        display_bytes[pos++] = (i < 7) ? "1-9 Keys"[i] : ' ';
    }
    
    /* Null terminate */
    display_bytes[pos] = '\0';
    
    millennium_client_set_display(client, display_bytes);
}

static void jukebox_show_playing(void) {
    if (jukebox_data.selected_song >= 0 && jukebox_data.selected_song < (int)NUM_SONGS) {
        const song_info_t *song = &songs[jukebox_data.selected_song];
        
        char display_bytes[100];
        size_t pos = 0;
        int i;
        
        /* Add line1, padded or truncated to 20 characters */
        for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
            display_bytes[pos++] = (i < (int)strlen(song->title)) ? song->title[i] : ' ';
        }
        
        /* Add line feed */
        display_bytes[pos++] = 0x0A;
        
        /* Add line2, padded or truncated to 20 characters */
        for (i = 0; i < 20 && pos < sizeof(display_bytes) - 1; i++) {
            display_bytes[pos++] = (i < (int)strlen(song->artist)) ? song->artist[i] : ' ';
        }
        
        /* Null terminate */
        display_bytes[pos] = '\0';
        
        millennium_client_set_display(client, display_bytes);
    }
}

static void jukebox_play_song(int song_number) {
    if (song_number < 0 || song_number >= (int)NUM_SONGS) {
        return;
    }
    
    jukebox_data.selected_song = song_number;
    jukebox_data.is_playing = 1;
    jukebox_data.play_start_time = time(NULL);
    jukebox_data.play_duration_seconds = songs[song_number].duration_seconds;
    
    jukebox_show_playing();
    
    const song_info_t *song = &songs[song_number];
    char log_msg[256];
    sprintf(log_msg, "Playing song: %s by %s", song->title, song->artist);
    logger_info_with_category("Jukebox", log_msg);
}

static void jukebox_stop_song(void) {
    if (jukebox_data.is_playing) {
        jukebox_data.is_playing = 0;
        jukebox_data.selected_song = -1;
        
        logger_info_with_category("Jukebox", "Song stopped");
        
        /* Return to menu or welcome */
        if (jukebox_data.inserted_cents >= jukebox_data.song_cost_cents) {
            jukebox_show_menu();
        } else {
            jukebox_show_welcome();
        }
    }
}

/* Helper function for future playback monitoring - kept for extensibility */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void jukebox_check_playback(void) {
    if (jukebox_data.is_playing) {
        time_t now = time(NULL);
        if (now - jukebox_data.play_start_time >= jukebox_data.play_duration_seconds) {
            /* Song finished */
            jukebox_stop_song();
        }
    }
}
#pragma GCC diagnostic pop

/* Plugin registration function */
void register_jukebox_plugin(void) {
    /* Initialize plugin data */
    jukebox_data.inserted_cents = 0;
    jukebox_data.song_cost_cents = 25; /* 25 cents per song */
    jukebox_data.selected_song = -1;
    jukebox_data.is_playing = 0;
    jukebox_data.last_activity = time(NULL);
    jukebox_data.play_start_time = 0;
    jukebox_data.play_duration_seconds = 0;
    
    plugins_register("Jukebox",
                    "Coin-operated music player",
                    jukebox_handle_coin,
                    jukebox_handle_keypad,
                    jukebox_handle_hook,
                    jukebox_handle_call_state);
}

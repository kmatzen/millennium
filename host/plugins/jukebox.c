#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "../plugins.h"
#include "../logger.h"
#include "../millennium_sdk.h"
#include "../display_manager.h"
#include "../audio_tones.h"

/* ALSA support - only on Linux */
#ifdef __linux__
#include <alsa/asoundlib.h>
#define HAVE_ALSA 1
#else
#define HAVE_ALSA 0
#endif

/* Jukebox plugin data */
typedef struct {
    int inserted_cents;
    int song_cost_cents;
    int selected_song;
    int is_playing;
    time_t last_activity;
    time_t play_start_time;
    int play_duration_seconds;
#if HAVE_ALSA
    snd_pcm_t *pcm_handle;  /* ALSA PCM handle */
#endif
    pthread_t audio_thread;  /* Audio playback thread */
    int stop_audio;  /* Flag to stop audio thread */
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
    const char *audio_file;
} song_info_t;

static const song_info_t songs[] = {
    {"Bohemian Rhapsody", "Queen", 355, "/usr/share/millennium/music/bohemian_rhapsody.wav"},
    {"Hotel California", "Eagles", 391, "/usr/share/millennium/music/hotel_california.wav"},
    {"Stairway to Heaven", "Led Zeppelin", 482, "/usr/share/millennium/music/stairway_to_heaven.wav"},
    {"Sweet Child O Mine", "Guns N Roses", 356, "/usr/share/millennium/music/sweet_child_o_mine.wav"},
    {"Imagine", "John Lennon", 183, "/usr/share/millennium/music/imagine.wav"},
    {"Billie Jean", "Michael Jackson", 294, "/usr/share/millennium/music/billie_jean.wav"},
    {"Like a Rolling Stone", "Bob Dylan", 366, "/usr/share/millennium/music/like_a_rolling_stone.wav"},
    {"Smells Like Teen Spirit", "Nirvana", 301, "/usr/share/millennium/music/smells_like_teen_spirit.wav"},
    {"What's Going On", "Marvin Gaye", 233, "/usr/share/millennium/music/whats_going_on.wav"}
};

#define NUM_SONGS (sizeof(songs) / sizeof(songs[0]))

/* Audio functions */
static int jukebox_play_wav_file(const char* wav_file);
static void jukebox_stop_audio(void);
static void* jukebox_audio_thread(void* arg);
#if HAVE_ALSA
static int jukebox_init_alsa(void);
static void jukebox_cleanup_alsa(void);
#endif

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
static void jukebox_on_activation(void) {
    /* Reset state and show welcome when plugin is activated */
    jukebox_data.inserted_cents = 0;
    jukebox_data.selected_song = -1;
    jukebox_data.is_playing = 0;
    jukebox_data.last_activity = time(NULL);
    jukebox_data.play_start_time = 0;
    jukebox_data.play_duration_seconds = 0;
    jukebox_data.stop_audio = 0;
    jukebox_show_welcome();
}

static void jukebox_show_welcome(void) {
    char line1[21];
    char line2[21];
    
    /* Check if receiver is down - show lift instruction */
    if (daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_DOWN) {
        strcpy(line1, "Lift receiver");
        strcpy(line2, "to play music");
    } else if (jukebox_data.inserted_cents > 0) {
        snprintf(line1, sizeof(line1), "Have: %dc", jukebox_data.inserted_cents);
        snprintf(line2, sizeof(line2), "Need: %dc", jukebox_data.song_cost_cents - jukebox_data.inserted_cents);
    } else {
        snprintf(line1, sizeof(line1), "Insert %dc", jukebox_data.song_cost_cents);
        strcpy(line2, "to play music");
    }
    
    display_manager_set_text(line1, line2);
}

static void jukebox_show_menu(void) {
    display_manager_set_text("Select Song", "1-9 Keys");
}

static void jukebox_show_playing(void) {
    if (jukebox_data.selected_song >= 0 && jukebox_data.selected_song < (int)NUM_SONGS) {
        const song_info_t *song = &songs[jukebox_data.selected_song];
        
        display_manager_set_text(song->title, song->artist);
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
    snprintf(log_msg, sizeof(log_msg), "Playing song: %s by %s", song->title, song->artist);
    logger_info_with_category("Jukebox", log_msg);
    
    /* Play the WAV file using ALSA */
    const char* wav_file = songs[song_number].audio_file;
    if (jukebox_play_wav_file(wav_file) == 0) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Started playing WAV file: %s", wav_file);
        logger_info_with_category("Jukebox", log_msg);
    } else {
        logger_warn_with_category("Jukebox", "Failed to play WAV file, falling back to tone");
        /* Use in-process tone (no external beep dep); no-op if ALSA unavailable (#130) */
        audio_tones_play_coin_tone();
    }
}

static void jukebox_stop_song(void) {
    if (jukebox_data.is_playing) {
        jukebox_data.is_playing = 0;
        jukebox_data.selected_song = -1;
        
        logger_info_with_category("Jukebox", "Song stopped");
        
        /* Stop the audio player */
        jukebox_stop_audio();
        
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

/* Audio functions */
#if HAVE_ALSA
static int jukebox_init_alsa(void) {
    int err;
    
    /* Open PCM device for playback */
    err = snd_pcm_open(&jukebox_data.pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Cannot open PCM device: %s", snd_strerror(err));
        logger_error_with_category("Jukebox", log_msg);
        return -1;
    }
    
    /* Set hardware parameters */
    snd_pcm_hw_params_t *hw_params;
    hw_params = malloc(snd_pcm_hw_params_sizeof());
    if (!hw_params) {
        logger_error_with_category("Jukebox", "Cannot allocate hardware parameters");
        snd_pcm_close(jukebox_data.pcm_handle);
        return -1;
    }
    
    err = snd_pcm_hw_params_any(jukebox_data.pcm_handle, hw_params);
    if (err < 0) {
        logger_error_with_category("Jukebox", "Cannot initialize hardware parameter structure");
        snd_pcm_close(jukebox_data.pcm_handle);
        free(hw_params);
        return -1;
    }
    
    /* Set access type */
    err = snd_pcm_hw_params_set_access(jukebox_data.pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        logger_error_with_category("Jukebox", "Cannot set access type");
        snd_pcm_close(jukebox_data.pcm_handle);
        free(hw_params);
        return -1;
    }
    
    /* Set sample format */
    err = snd_pcm_hw_params_set_format(jukebox_data.pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        logger_error_with_category("Jukebox", "Cannot set sample format");
        snd_pcm_close(jukebox_data.pcm_handle);
        free(hw_params);
        return -1;
    }
    
    /* Set sample rate */
    unsigned int rate = 44100;
    err = snd_pcm_hw_params_set_rate_near(jukebox_data.pcm_handle, hw_params, &rate, 0);
    if (err < 0) {
        logger_error_with_category("Jukebox", "Cannot set sample rate");
        snd_pcm_close(jukebox_data.pcm_handle);
        free(hw_params);
        return -1;
    }
    
    /* Set number of channels */
    err = snd_pcm_hw_params_set_channels(jukebox_data.pcm_handle, hw_params, 2);
    if (err < 0) {
        logger_error_with_category("Jukebox", "Cannot set channel count");
        snd_pcm_close(jukebox_data.pcm_handle);
        free(hw_params);
        return -1;
    }
    
    /* Apply hardware parameters */
    err = snd_pcm_hw_params(jukebox_data.pcm_handle, hw_params);
    if (err < 0) {
        logger_error_with_category("Jukebox", "Cannot set hardware parameters");
        snd_pcm_close(jukebox_data.pcm_handle);
        free(hw_params);
        return -1;
    }
    
    /* Free hardware parameters */
    free(hw_params);
    
    logger_info_with_category("Jukebox", "ALSA initialized successfully");
    return 0;
}

/* Helper function for future cleanup - kept for extensibility */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void jukebox_cleanup_alsa(void) {
    if (jukebox_data.pcm_handle) {
        snd_pcm_close(jukebox_data.pcm_handle);
        jukebox_data.pcm_handle = NULL;
        logger_info_with_category("Jukebox", "ALSA cleaned up");
    }
}
#pragma GCC diagnostic pop
#endif

static int jukebox_play_wav_file(const char* wav_file) {
#if HAVE_ALSA
    /* Initialize ALSA if not already done */
    if (!jukebox_data.pcm_handle) {
        if (jukebox_init_alsa() < 0) {
            return -1;
        }
    }
#endif
    
    /* Set stop flag to false */
    jukebox_data.stop_audio = 0;
    
    /* Create audio thread */
    int err = pthread_create(&jukebox_data.audio_thread, NULL, jukebox_audio_thread, (void*)wav_file);
    if (err != 0) {
        logger_error_with_category("Jukebox", "Failed to create audio thread");
        return -1;
    }
    
    logger_info_with_category("Jukebox", "Started audio thread for WAV file");
    return 0;
}

static void* jukebox_audio_thread(void* arg) {
    const char* wav_file = (const char*)arg;
    FILE *file;
    char buffer[4096];
    size_t bytes_read;
    
    /* Open WAV file */
    file = fopen(wav_file, "rb");
    if (!file) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Cannot open WAV file: %s", wav_file);
        logger_error_with_category("Jukebox", log_msg);
        return NULL;
    }
    
    /* Skip WAV header (44 bytes) */
    fseek(file, 44, SEEK_SET);
    
#if HAVE_ALSA
    /* Play audio data using ALSA */
    while (!jukebox_data.stop_audio && (bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        snd_pcm_sframes_t frames_written = snd_pcm_writei(jukebox_data.pcm_handle, buffer, bytes_read / 4);
        if (frames_written < 0) {
            /* Recover from underrun */
            frames_written = snd_pcm_recover(jukebox_data.pcm_handle, frames_written, 0);
            if (frames_written < 0) {
                logger_error_with_category("Jukebox", "Cannot recover from underrun");
                break;
            }
        }
    }
#else
    /* Fallback: just read the file to simulate playback */
    while (!jukebox_data.stop_audio && (bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        /* Simulate playback time - use sleep for C89 compatibility */
        /* Note: sleep(0) is not portable, so we'll just continue */
    }
#endif
    
    fclose(file);
    
    /* Mark as finished */
    jukebox_data.is_playing = 0;
    jukebox_data.selected_song = -1;
    
    logger_info_with_category("Jukebox", "WAV file playback finished");
    return NULL;
}

static void jukebox_stop_audio(void) {
    jukebox_data.stop_audio = 1;
    
#if HAVE_ALSA
    if (jukebox_data.pcm_handle) {
        snd_pcm_drop(jukebox_data.pcm_handle);
        snd_pcm_prepare(jukebox_data.pcm_handle);
    }
#endif
    
    /* Wait for audio thread to finish */
    pthread_join(jukebox_data.audio_thread, NULL);
    
    logger_info_with_category("Jukebox", "Audio stopped");
}

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
#if HAVE_ALSA
    jukebox_data.pcm_handle = NULL;
#endif
    jukebox_data.stop_audio = 0;
    
    plugins_register("Jukebox",
                    "Coin-operated music player",
                    jukebox_handle_coin,
                    jukebox_handle_keypad,
                    jukebox_handle_hook,
                    jukebox_handle_call_state,
                    NULL,
                    jukebox_on_activation,
                    NULL);
}

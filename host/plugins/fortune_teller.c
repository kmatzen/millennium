#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../plugins.h"
#include "../logger.h"
#include "../millennium_sdk.h"
#include "../display_manager.h"

/* Delay states: non-blocking fortune flow (#114) */
#define FT_STATE_IDLE 0
#define FT_STATE_READING 1
#define FT_STATE_SHOWING 2

/* Fortune teller plugin data */
typedef struct {
    int inserted_cents;
    int fortune_cost_cents;
    int fortune_type;
    int is_ready;
    time_t last_activity;
    int delay_state;
    time_t delay_until;
} fortune_teller_data_t;

static fortune_teller_data_t fortune_teller_data = {0};

/* External references */
extern daemon_state_data_t *daemon_state;
extern millennium_client_t *client;

/* Fortune categories */
static const char* fortune_categories[] = {
    "Love",
    "Career", 
    "Health",
    "Money",
    "General"
};

/* Sample fortunes for each category */
static const char* love_fortunes[] = {
    "A new romance will blossom soon",
    "Your heart will find its match",
    "Love is written in the stars for you",
    "A special someone is thinking of you"
};

static const char* career_fortunes[] = {
    "Great success awaits in your work",
    "A promotion is on the horizon",
    "Your talents will be recognized",
    "New opportunities will present themselves"
};

static const char* health_fortunes[] = {
    "Your vitality will increase",
    "Good health will be your companion",
    "Energy and strength will return",
    "Wellness is your destiny"
};

static const char* money_fortunes[] = {
    "Financial abundance is coming",
    "Your investments will prosper",
    "Money will flow to you easily",
    "Wealth and security await"
};

static const char* general_fortunes[] = {
    "Good fortune follows you",
    "Your path is blessed with luck",
    "Positive changes are coming",
    "The universe smiles upon you"
};

/* Internal functions */
static void fortune_teller_show_welcome(void);
static void fortune_teller_show_menu(void);
static void fortune_teller_show_reading(void);
static void fortune_teller_give_fortune(void);
static const char* fortune_teller_get_random_fortune(int category);

/* Fortune teller event handlers */
static int fortune_teller_handle_coin(int coin_value, const char *coin_code) {
    if (coin_value > 0) {
        fortune_teller_data.inserted_cents += coin_value;
        fortune_teller_data.last_activity = time(NULL);
        
        if (fortune_teller_data.inserted_cents >= fortune_teller_data.fortune_cost_cents) {
            fortune_teller_data.is_ready = 1;
            fortune_teller_show_menu();
        } else {
            fortune_teller_show_welcome();
        }
        
        logger_infof_with_category("FortuneTeller", 
                "Coin inserted: %s, value: %d cents, total: %d cents",
                coin_code, coin_value, fortune_teller_data.inserted_cents);
    }
    return 0;
}

static int fortune_teller_handle_keypad(char key) {
    if (!fortune_teller_data.is_ready) {
        return 0; /* Not ready for input */
    }
    
    if (key >= '1' && key <= '5') {
        fortune_teller_data.fortune_type = key - '1'; /* Convert to 0-4 */
        fortune_teller_data.inserted_cents -= fortune_teller_data.fortune_cost_cents;
        fortune_teller_data.is_ready = 0;
        fortune_teller_give_fortune();
    }
    return 0;
}

static int fortune_teller_handle_hook(int hook_up, int hook_down) {
    if (hook_up) {
        /* Reset for new session */
        fortune_teller_data.inserted_cents = 0;
        fortune_teller_data.fortune_type = 0;
        fortune_teller_data.is_ready = 0;
        fortune_teller_data.last_activity = time(NULL);
        fortune_teller_show_welcome();
    } else if (hook_down) {
        /* Return coins if handset down without fortune */
        if (fortune_teller_data.inserted_cents > 0) {
            fortune_teller_data.inserted_cents = 0;
        }
        fortune_teller_data.fortune_type = 0;
        fortune_teller_data.is_ready = 0;
        fortune_teller_show_welcome();
    }
    return 0;
}

static int fortune_teller_handle_call_state(int call_state) {
    /* Fortune teller doesn't handle calls */
    (void)call_state; /* Suppress unused parameter warning */
    return 0;
}

/* Internal function implementations */
static void fortune_teller_on_activation(void) {
    fortune_teller_data.inserted_cents = 0;
    fortune_teller_data.fortune_type = 0;
    fortune_teller_data.is_ready = 0;
    fortune_teller_data.delay_state = FT_STATE_IDLE;
    fortune_teller_data.last_activity = time(NULL);
    fortune_teller_show_welcome();
}

static void fortune_teller_show_welcome(void) {
    char line1[21];
    char line2[21];
    
    /* Check if receiver is down - show lift instruction */
    if (daemon_state && daemon_state->current_state == DAEMON_STATE_IDLE_DOWN) {
        strcpy(line1, "Lift receiver");
        strcpy(line2, "for fortune");
    } else if (fortune_teller_data.inserted_cents > 0) {
        snprintf(line1, sizeof(line1), "Have: %dc", fortune_teller_data.inserted_cents);
        snprintf(line2, sizeof(line2), "Need: %dc", fortune_teller_data.fortune_cost_cents - fortune_teller_data.inserted_cents);
    } else {
        snprintf(line1, sizeof(line1), "Insert %dc", fortune_teller_data.fortune_cost_cents);
        strcpy(line2, "for your fortune");
    }
    
    display_manager_set_text(line1, line2);
}

static void fortune_teller_show_menu(void) {
    display_manager_set_text("Choose Fortune", "1-5 Keys");
}

static void fortune_teller_show_reading(void) {
    display_manager_set_text("Reading", "Crystal...");
}

static void fortune_teller_give_fortune(void) {
    fortune_teller_show_reading();
    fortune_teller_data.delay_state = FT_STATE_READING;
    fortune_teller_data.delay_until = time(NULL) + 2;
}

static void fortune_teller_tick(void) {
    time_t now = time(NULL);
    if (fortune_teller_data.delay_state == FT_STATE_READING && now >= fortune_teller_data.delay_until) {
        const char* fortune = fortune_teller_get_random_fortune(fortune_teller_data.fortune_type);
        const char* category = fortune_categories[fortune_teller_data.fortune_type];
        display_manager_set_text(category, fortune);
        {
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "Fortune given: %s - %s", category, fortune);
            logger_info_with_category("FortuneTeller", log_msg);
        }
        fortune_teller_data.inserted_cents = 0;
        fortune_teller_data.fortune_type = 0;
        fortune_teller_data.is_ready = 0;
        fortune_teller_data.delay_state = FT_STATE_SHOWING;
        fortune_teller_data.delay_until = now + 3;
    } else if (fortune_teller_data.delay_state == FT_STATE_SHOWING && now >= fortune_teller_data.delay_until) {
        fortune_teller_data.delay_state = FT_STATE_IDLE;
        fortune_teller_show_welcome();
    }
}

static const char* fortune_teller_get_random_fortune(int category) {
    const char** fortunes = NULL;
    int count = 0;
    
    switch (category) {
        case 0: fortunes = love_fortunes; count = 4; break;
        case 1: fortunes = career_fortunes; count = 4; break;
        case 2: fortunes = health_fortunes; count = 4; break;
        case 3: fortunes = money_fortunes; count = 4; break;
        case 4: fortunes = general_fortunes; count = 4; break;
        default: return "The future is unclear";
    }
    
    if (fortunes && count > 0) {
        return fortunes[rand() % count];
    }
    
    return "The future is unclear";
}

/* Plugin registration function */
void register_fortune_teller_plugin(void) {
    /* Initialize plugin data */
    fortune_teller_data.inserted_cents = 0;
    fortune_teller_data.fortune_cost_cents = 25; /* 25 cents per fortune */
    fortune_teller_data.fortune_type = 0;
    fortune_teller_data.is_ready = 0;
    fortune_teller_data.last_activity = time(NULL);
    
    /* Seed random number generator */
    srand(time(NULL));
    
    plugins_register("Fortune Teller",
                    "Mystical fortune telling experience",
                    fortune_teller_handle_coin,
                    fortune_teller_handle_keypad,
                    fortune_teller_handle_hook,
                    fortune_teller_handle_call_state,
                    NULL,
                    fortune_teller_on_activation,
                    fortune_teller_tick);
}

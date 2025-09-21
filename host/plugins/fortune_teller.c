#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../plugins.h"
#include "../logger.h"
#include "../millennium_sdk.h"

/* Fortune teller plugin data */
typedef struct {
    int inserted_cents;
    int fortune_cost_cents;
    int fortune_type;
    int is_ready;
    time_t last_activity;
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
    /* Reset state and show welcome when plugin is activated */
    fortune_teller_data.inserted_cents = 0;
    fortune_teller_data.fortune_type = 0;
    fortune_teller_data.is_ready = 0;
    fortune_teller_data.last_activity = time(NULL);
    fortune_teller_show_welcome();
}

static void fortune_teller_show_welcome(void) {
    char line1[21];
    char line2[21];
    
    if (fortune_teller_data.inserted_cents > 0) {
        sprintf(line1, "Have: %dc", fortune_teller_data.inserted_cents);
        sprintf(line2, "Need: %dc", fortune_teller_data.fortune_cost_cents - fortune_teller_data.inserted_cents);
    } else {
        sprintf(line1, "Insert %dc", fortune_teller_data.fortune_cost_cents);
        strcpy(line2, "for your fortune");
    }
    
    char display_bytes[100];
    size_t pos = 0;
    int i;
    
    /* Add line1, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
        display_bytes[pos++] = (i < (int)strlen(line1)) ? line1[i] : ' ';
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

static void fortune_teller_show_menu(void) {
    char display_bytes[100];
    size_t pos = 0;
    int i;
    
    /* Add line1, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
        display_bytes[pos++] = (i < 12) ? "Choose Fortune"[i] : ' ';
    }
    
    /* Add line feed */
    display_bytes[pos++] = 0x0A;
    
    /* Add line2, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 1; i++) {
        display_bytes[pos++] = (i < 10) ? "1-5 Keys"[i] : ' ';
    }
    
    /* Null terminate */
    display_bytes[pos] = '\0';
    
    millennium_client_set_display(client, display_bytes);
}

static void fortune_teller_show_reading(void) {
    char display_bytes[100];
    size_t pos = 0;
    int i;
    
    /* Add line1, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
        display_bytes[pos++] = (i < 7) ? "Reading"[i] : ' ';
    }
    
    /* Add line feed */
    display_bytes[pos++] = 0x0A;
    
    /* Add line2, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 1; i++) {
        display_bytes[pos++] = (i < 8) ? "Crystal..."[i] : ' ';
    }
    
    /* Null terminate */
    display_bytes[pos] = '\0';
    
    millennium_client_set_display(client, display_bytes);
}

static void fortune_teller_give_fortune(void) {
    fortune_teller_show_reading();
    
    /* Simulate mystical reading time */
    sleep(2);
    
    const char* fortune = fortune_teller_get_random_fortune(fortune_teller_data.fortune_type);
    const char* category = fortune_categories[fortune_teller_data.fortune_type];
    
    /* Show fortune category and text */
    char display_bytes[100];
    size_t pos = 0;
    int i;
    
    /* Add line1: Category name, padded or truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 2; i++) {
        display_bytes[pos++] = (i < (int)strlen(category)) ? category[i] : ' ';
    }
    
    /* Add line feed */
    display_bytes[pos++] = 0x0A;
    
    /* Add line2: Fortune text, truncated to 20 characters */
    for (i = 0; i < 20 && pos < sizeof(display_bytes) - 1; i++) {
        display_bytes[pos++] = (i < (int)strlen(fortune)) ? fortune[i] : ' ';
    }
    
    /* Null terminate */
    display_bytes[pos] = '\0';
    
    millennium_client_set_display(client, display_bytes);
    
    /* Log the fortune */
    char log_msg[256];
    sprintf(log_msg, "Fortune given: %s - %s", category, fortune);
    logger_info_with_category("FortuneTeller", log_msg);
    
    /* Reset for next fortune */
    fortune_teller_data.inserted_cents = 0;
    fortune_teller_data.fortune_type = 0;
    fortune_teller_data.is_ready = 0;
    
    /* Return to welcome after a delay */
    sleep(3);
    fortune_teller_show_welcome();
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
                    fortune_teller_on_activation);
}

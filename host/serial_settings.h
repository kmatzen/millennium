#ifndef SERIAL_SETTINGS_H
#define SERIAL_SETTINGS_H

#include "config.h"

typedef struct {
    char device_path[256];
    int baud_rate;
} serial_settings_t;

int serial_settings_load(const config_data_t *config, serial_settings_t *settings);

#endif

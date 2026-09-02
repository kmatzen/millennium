#include "serial_settings.h"
#include <string.h>

int serial_settings_load(const config_data_t *config, serial_settings_t *settings) {
    const char *path;
    if (!config || !settings) return -1;
    path = config_get_display_device(config);
    if (!path || !path[0] || strlen(path) >= sizeof(settings->device_path)) return -1;
    strcpy(settings->device_path, path);
    settings->baud_rate = config_get_baud_rate(config);
    return 0;
}

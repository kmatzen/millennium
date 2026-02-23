#include "baresip_interface.h"
#include <re.h>
#include <baresip.h>
#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re_dbg.h>
#include <string.h>


/* Core library functions */
int baresip_libre_init(void) {
    return libre_init();
}

void baresip_libre_close(void) {
    libre_close();
}

int baresip_re_thread_async_init(int workers) {
    return re_thread_async_init(workers);
}

void baresip_re_thread_async_close(void) {
    re_thread_async_close();
}

void baresip_re_main(void *arg) {
    re_main(arg);
}

/* Debug functions */
void baresip_log_enable_debug(int enable) {
    log_enable_debug(enable);
}

void baresip_dbg_init(int level, int flags) {
    dbg_init(level, flags);
}

/* Configuration functions */
int baresip_conf_configure(void) {
    return conf_configure();
}

void baresip_conf_close(void) {
    conf_close();
}

struct config *baresip_conf_config(void) {
    return conf_config();
}

int baresip_conf_modules(void) {
    return conf_modules();
}

/* Baresip initialization */
int baresip_init_c(struct config *config) {
    return baresip_init(config);
}

void baresip_close_c(void) {
    baresip_close();
}

/* Player functions */
struct player *baresip_player_c(void) {
    return baresip_player();
}

void baresip_play_set_path(struct player *player, const char *path) {
    play_set_path(player, path);
}

/* UA (User Agent) functions */
int baresip_ua_init(const char *name, int enable_rtp, int enable_video, int enable_ice) {
    return ua_init(name, enable_rtp, enable_video, (enum vidmode)enable_ice);
}

void baresip_ua_close(void) {
    ua_close();
}

void baresip_ua_stop_all(int forced) {
    ua_stop_all(forced);
}

struct ua *baresip_ua_find_requri(const char *uri) {
    struct pl pluri;
    pl_set_str(&pluri, uri);
    return uag_find_requri_pl(&pluri);
}

struct call *baresip_ua_call(struct ua *ua) {
    return ua_call(ua);
}

int baresip_ua_connect(struct ua *ua, struct call **call, const char *from, const char *uri, int video_mode) {
    return ua_connect(ua, call, from, uri, (enum vidmode)video_mode);
}

int baresip_ua_answer(struct ua *ua, struct call *call, int video_mode) {
    return ua_answer(ua, call, (enum vidmode)video_mode);
}

void baresip_ua_hangup(struct ua *ua, struct call *call, int code, const char *reason) {
    ua_hangup(ua, call, code, reason);
}

/* Account functions */
struct account *baresip_ua_account(struct ua *ua) {
    return ua_account(ua);
}

const char *baresip_account_aor(struct account *account) {
    return account_aor(account);
}

int baresip_account_uri_complete_strdup(struct account *account, char **uri, const char *pl) {
    struct pl pluri;
    pl_set_str(&pluri, pl);
    return account_uri_complete_strdup(account, uri, &pluri);
}

/* Call functions */
struct ua *baresip_call_get_ua(struct call *call) {
    return call_get_ua(call);
}

/* Event functions - thunk to bridge enum baresip_ua_event <-> enum ua_event */
static baresip_ua_event_handler_t g_bevent_handler;
static void *g_bevent_arg;

static void bevent_thunk(enum ua_event ev, struct bevent *event, void *arg) {
    (void)arg;
    if (g_bevent_handler)
        g_bevent_handler((enum baresip_ua_event)ev, event, g_bevent_arg);
}

void baresip_bevent_register(baresip_ua_event_handler_t handler, void *arg) {
    g_bevent_handler = handler;
    g_bevent_arg = arg;
    bevent_register(bevent_thunk, NULL);
}

struct call *baresip_bevent_get_call(const struct bevent *event) {
    return bevent_get_call(event);
}

struct ua *baresip_bevent_get_ua(const struct bevent *event) {
    return bevent_get_ua(event);
}

const char *baresip_bevent_get_text(const struct bevent *event) {
    return bevent_get_text(event);
}

const char *baresip_uag_event_str(enum baresip_ua_event ev) {
    return uag_event_str((enum ua_event)ev);
}

int baresip_ua_isregistered(const struct ua *ua) {
    return ua_isregistered(ua) ? 1 : 0;
}

/* Audio device listing */
struct le *baresip_ausrcl_head(void) {
    return list_head(baresip_ausrcl());
}

struct le *baresip_auplayl_head(void) {
    return list_head(baresip_auplayl());
}

struct le *baresip_list_next(struct le *le) {
    return le->next;
}

struct ausrc *baresip_list_data(struct le *le) {
    return le->data;
}

struct auplay *baresip_auplay_data(struct le *le) {
    return le->data;
}

const char *baresip_ausrc_name(struct ausrc *ausrc) {
    return ausrc->name;
}

const char *baresip_auplay_name(struct auplay *auplay) {
    return auplay->name;
}

/* Module functions */
void baresip_module_app_unload(void) {
    module_app_unload();
}

void baresip_mod_close(void) {
    mod_close();
}

/* Memory management */
void baresip_mem_deref(void *ptr) {
    mem_deref(ptr);
}

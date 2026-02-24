#ifndef BARESIP_INTERFACE_H
#define BARESIP_INTERFACE_H

#include <stdint.h>
#include <stddef.h>

struct call;
struct bevent;
struct ua;
struct auplay;
struct ausrc;
struct le;
struct player;
struct config;
struct account;

enum baresip_ua_event {
    BARESIP_UA_EVENT_REGISTERING = 0,
    BARESIP_UA_EVENT_REGISTER_OK,
    BARESIP_UA_EVENT_REGISTER_FAIL,
    BARESIP_UA_EVENT_UNREGISTERING,
    BARESIP_UA_EVENT_FALLBACK_OK,
    BARESIP_UA_EVENT_FALLBACK_FAIL,
    BARESIP_UA_EVENT_MWI_NOTIFY,
    BARESIP_UA_EVENT_CREATE,
    BARESIP_UA_EVENT_SHUTDOWN,
    BARESIP_UA_EVENT_EXIT,

    BARESIP_UA_EVENT_CALL_INCOMING,
    BARESIP_UA_EVENT_CALL_OUTGOING,
    BARESIP_UA_EVENT_CALL_RINGING,
    BARESIP_UA_EVENT_CALL_PROGRESS,
    BARESIP_UA_EVENT_CALL_ANSWERED,
    BARESIP_UA_EVENT_CALL_ESTABLISHED,
    BARESIP_UA_EVENT_CALL_CLOSED,
    BARESIP_UA_EVENT_CALL_TRANSFER,
    BARESIP_UA_EVENT_CALL_REDIRECT,
    BARESIP_UA_EVENT_CALL_TRANSFER_FAILED,
    BARESIP_UA_EVENT_CALL_DTMF_START,
    BARESIP_UA_EVENT_CALL_DTMF_END,
    BARESIP_UA_EVENT_CALL_RTPESTAB,
    BARESIP_UA_EVENT_CALL_RTCP,
    BARESIP_UA_EVENT_CALL_MENC,
    BARESIP_UA_EVENT_VU_TX,
    BARESIP_UA_EVENT_VU_RX,
    BARESIP_UA_EVENT_AUDIO_ERROR,
    BARESIP_UA_EVENT_CALL_LOCAL_SDP,      /**< param: offer or answer */
    BARESIP_UA_EVENT_CALL_REMOTE_SDP,     /**< param: offer or answer */
    BARESIP_UA_EVENT_CALL_HOLD,           /**< Call put on-hold by peer          */
    BARESIP_UA_EVENT_CALL_RESUME,         /**< Call resumed by peer              */
    BARESIP_UA_EVENT_REFER,
    BARESIP_UA_EVENT_MODULE,
    BARESIP_UA_EVENT_END_OF_FILE,
    BARESIP_UA_EVENT_CUSTOM,
    BARESIP_UA_EVENT_SIPSESS_CONN,

    BARESIP_UA_EVENT_MAX,
};

/* Function pointer types for callbacks */
typedef void (*baresip_ua_event_handler_t)(enum baresip_ua_event ev, struct bevent *event, void *arg);

/* Core library functions */
int baresip_libre_init(void);
void baresip_libre_close(void);
int baresip_re_thread_async_init(int workers);
void baresip_re_thread_async_close(void);
void baresip_re_main(void *arg);

/* Debug functions */
void baresip_log_enable_debug(int enable);
void baresip_dbg_init(int level, int flags);

/* Configuration functions */
int baresip_conf_configure(void);
void baresip_conf_close(void);
struct config *baresip_conf_config(void);
int baresip_conf_modules(void);

/* Baresip initialization */
int baresip_init_c(struct config *config);
void baresip_close_c(void);

/* Player functions */
struct player *baresip_player_c(void);
void baresip_play_set_path(struct player *player, const char *path);

/* UA (User Agent) functions */
int baresip_ua_init(const char *name, int enable_rtp, int enable_video, int enable_ice);
void baresip_ua_close(void);
void baresip_ua_stop_all(int forced);
struct ua *baresip_ua_find_requri(const char *uri);
struct call *baresip_ua_call(struct ua *ua);
int baresip_ua_connect(struct ua *ua, struct call **call, const char *from, const char *uri, int video_mode);
int baresip_ua_answer(struct ua *ua, struct call *call, int video_mode);
void baresip_ua_hangup(struct ua *ua, struct call *call, int code, const char *reason);

/* Account functions */
struct account *baresip_ua_account(struct ua *ua);
const char *baresip_account_aor(struct account *account);
int baresip_account_uri_complete_strdup(struct account *account, char **uri, const char *pl);

/* Call functions */
struct ua *baresip_call_get_ua(struct call *call);
int baresip_call_send_digit(struct call *call, char key);

/* Event functions */
void baresip_bevent_register(baresip_ua_event_handler_t handler, void *arg);
struct call *baresip_bevent_get_call(const struct bevent *event);
struct ua *baresip_bevent_get_ua(const struct bevent *event);
const char *baresip_bevent_get_text(const struct bevent *event);
const char *baresip_uag_event_str(enum baresip_ua_event ev);

/* UA registration status */
int baresip_ua_isregistered(const struct ua *ua);

/* Audio device listing */
struct le *baresip_ausrcl_head(void);
struct le *baresip_auplayl_head(void);
struct le *baresip_list_next(struct le *le);
struct ausrc *baresip_list_data(struct le *le);
struct auplay *baresip_auplay_data(struct le *le);
const char *baresip_ausrc_name(struct ausrc *ausrc);
const char *baresip_auplay_name(struct auplay *auplay);

/* Module functions */
void baresip_module_app_unload(void);
void baresip_mod_close(void);

/* Memory management */
void baresip_mem_deref(void *ptr);

/* Constants */
#define BARESIP_VIDMODE_OFF 0
#define BARESIP_ASYNC_WORKERS 4
#define BARESIP_DBG_DEBUG 4
#define BARESIP_DBG_ANSI 1
#define BARESIP_DBG_TIME 2

#endif /* BARESIP_INTERFACE_H */

/*
 * PJSIP (PJSUA C API) implementation. Built only on the Pi, where pjproject
 * is installed (the Makefile compiles this with `pkg-config --cflags
 * libpjproject`). See pjsip_interface.h for the rationale and the plain-C
 * surface used by millennium_sdk.c.
 */
#include "pjsip_interface.h"
#include "logger.h"

#include <pjsua-lib/pjsua.h>
#include <string.h>
#include <stdio.h>

#define THIS_FILE "pjsip_interface"

/* ── Module state ─────────────────────────────────────────────────────── */

static pjsua_acc_id  g_acc_id  = PJSUA_INVALID_ID;
static pjsua_call_id g_call_id = PJSUA_INVALID_ID; /* current call, if any */
static pjsip_iface_event_cb g_cb = NULL;
static void *g_cb_arg = NULL;
static int   g_registered = 0;
static int   g_started = 0;        /* 1 once pjsua_create/init/start succeeded */
static int   g_transport = PJSIP_IFACE_TRANSPORT_UDP;
static char  g_domain[128] = "";  /* host part of the AOR, for outbound URIs */

/* pj_str_t from a C string (PJSUA treats these as read-only). */
static pj_str_t S(const char *s) { return pj_str((char *)s); }

static void emit(enum pjsip_iface_event ev, const char *text) {
    if (g_cb) g_cb(ev, text, g_cb_arg);
}

/* Any non-PJSUA thread that calls a PJSUA/PJLIB function must be registered
 * first so it has thread-local storage. The daemon calls pjsip_iface_call/
 * answer/hangup/send_dtmf from its event threads, so register lazily. */
static void thread_ensure(void) {
    /* Each external thread needs its own persistent descriptor storage, so
     * these are thread-local (GCC/Clang __thread; available on the Pi's gcc). */
    static __thread pj_thread_desc desc;
    static __thread pj_thread_t *thread;
    if (!pj_thread_is_registered()) {
        pj_bzero(desc, sizeof(desc));
        pj_thread_register("millennium", desc, &thread);
    }
}

/* ── PJSUA callbacks (run on PJSUA worker threads) ────────────────────── */

static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
                             pjsip_rx_data *rdata) {
    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);
    /* Track the inbound call; the daemon decides whether/when to answer. */
    g_call_id = call_id;
    logger_info_with_category("SIP", "Incoming call");
    emit(PJSIP_IFACE_CALL_INCOMING, NULL);
}

static void on_call_state(pjsua_call_id call_id, pjsip_event *e) {
    pjsua_call_info ci;
    PJ_UNUSED_ARG(e);

    if (pjsua_call_get_info(call_id, &ci) != PJ_SUCCESS)
        return;

    logger_infof_with_category("SIP", "Call %d state: %.*s",
        call_id, (int)ci.state_text.slen, ci.state_text.ptr);

    if (ci.state == PJSIP_INV_STATE_CONFIRMED) {
        g_call_id = call_id;
        emit(PJSIP_IFACE_CALL_ESTABLISHED, NULL);
    } else if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
        if (call_id == g_call_id)
            g_call_id = PJSUA_INVALID_ID;
        emit(PJSIP_IFACE_CALL_CLOSED,
             ci.last_status_text.slen ? ci.last_status_text.ptr : NULL);
    }
}

/* Connect the call's audio to the sound device when media becomes active. */
static void on_call_media_state(pjsua_call_id call_id) {
    pjsua_call_info ci;
    if (pjsua_call_get_info(call_id, &ci) != PJ_SUCCESS)
        return;
    if (ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
        pjsua_conf_connect(ci.conf_slot, 0); /* call -> speaker (earpiece) */
        pjsua_conf_connect(0, ci.conf_slot); /* mic  -> call               */
    }
}

static void on_reg_state2(pjsua_acc_id acc_id, pjsua_reg_info *info) {
    int code = (info && info->cbparam) ? info->cbparam->code : 0;
    const char *reason = NULL;
    PJ_UNUSED_ARG(acc_id);

    if (info && info->cbparam && info->cbparam->reason.slen)
        reason = info->cbparam->reason.ptr;

    if (code >= 200 && code < 300) {
        g_registered = 1;
        logger_info_with_category("SIP", "Registration OK");
        emit(PJSIP_IFACE_REG_OK, NULL);
    } else {
        g_registered = 0;
        logger_warnf_with_category("SIP", "Registration failed: %d %s",
            code, reason ? reason : "");
        emit(PJSIP_IFACE_REG_FAIL, reason);
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

/* Extract the host part of "sip:user@host[;params]" into g_domain. */
static void extract_domain(const char *id_uri) {
    const char *at = id_uri ? strchr(id_uri, '@') : NULL;
    size_t i = 0;
    g_domain[0] = '\0';
    if (!at) return;
    at++;
    while (at[i] && at[i] != ';' && at[i] != '>' && i < sizeof(g_domain) - 1) {
        g_domain[i] = at[i];
        i++;
    }
    g_domain[i] = '\0';
}

int pjsip_iface_start(const pjsip_iface_account_t *acc,
                      pjsip_iface_event_cb cb, void *arg) {
    pj_status_t status;
    pjsua_config ua_cfg;
    pjsua_logging_config log_cfg;
    pjsua_media_config media_cfg;
    pjsua_transport_config tp_cfg;
    pjsua_acc_config acc_cfg;
    pjsip_transport_type_e tp_type;

    if (!acc || !acc->id_uri || !acc->reg_uri) {
        logger_error_with_category("SIP", "Invalid account configuration");
        return -1;
    }

    g_cb = cb;
    g_cb_arg = arg;
    g_transport = acc->transport;
    extract_domain(acc->id_uri);

    status = pjsua_create();
    if (status != PJ_SUCCESS) {
        logger_error_with_category("SIP", "pjsua_create failed");
        return -1;
    }

    /* Core config + callbacks */
    pjsua_config_default(&ua_cfg);
    ua_cfg.cb.on_incoming_call   = &on_incoming_call;
    ua_cfg.cb.on_call_state      = &on_call_state;
    ua_cfg.cb.on_call_media_state = &on_call_media_state;
    ua_cfg.cb.on_reg_state2      = &on_reg_state2;
    if (acc->stun_server && acc->stun_server[0]) {
        ua_cfg.stun_srv_cnt = 1;
        ua_cfg.stun_srv[0]  = S(acc->stun_server);
    }

    pjsua_logging_config_default(&log_cfg);
    log_cfg.console_level = 4;

    pjsua_media_config_default(&media_cfg);
    media_cfg.clock_rate = 8000;        /* match G.711 telephony */
    media_cfg.snd_clock_rate = 8000;
    media_cfg.channel_count = 1;        /* mono handset */

    status = pjsua_init(&ua_cfg, &log_cfg, &media_cfg);
    if (status != PJ_SUCCESS) {
        logger_error_with_category("SIP", "pjsua_init failed");
        pjsua_destroy();
        return -1;
    }

    /* Signalling transport */
    pjsua_transport_config_default(&tp_cfg);
    tp_cfg.port = (acc->local_port > 0) ? (unsigned)acc->local_port : 0;
    switch (acc->transport) {
        case PJSIP_IFACE_TRANSPORT_TCP: tp_type = PJSIP_TRANSPORT_TCP; break;
        case PJSIP_IFACE_TRANSPORT_TLS: tp_type = PJSIP_TRANSPORT_TLS; break;
        default:                        tp_type = PJSIP_TRANSPORT_UDP; break;
    }
    status = pjsua_transport_create(tp_type, &tp_cfg, NULL);
    if (status != PJ_SUCCESS) {
        logger_error_with_category("SIP", "pjsua_transport_create failed");
        pjsua_destroy();
        return -1;
    }

    status = pjsua_start();
    if (status != PJ_SUCCESS) {
        logger_error_with_category("SIP", "pjsua_start failed");
        pjsua_destroy();
        return -1;
    }

    /* Optional explicit sound device selection (else PJSUA auto/default). */
    if (acc->snd_capture_dev >= 0 || acc->snd_playback_dev >= 0) {
        pjsua_set_snd_dev(acc->snd_capture_dev, acc->snd_playback_dev);
    }

    /* Register the account */
    pjsua_acc_config_default(&acc_cfg);
    acc_cfg.id      = S(acc->id_uri);
    acc_cfg.reg_uri = S(acc->reg_uri);
    acc_cfg.cred_count = 1;
    acc_cfg.cred_info[0].realm     = S((acc->realm && acc->realm[0]) ? acc->realm : "*");
    acc_cfg.cred_info[0].scheme    = S("digest");
    acc_cfg.cred_info[0].username  = S(acc->username ? acc->username : "");
    acc_cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
    acc_cfg.cred_info[0].data      = S(acc->password ? acc->password : "");

    status = pjsua_acc_add(&acc_cfg, PJ_TRUE, &g_acc_id);
    if (status != PJ_SUCCESS) {
        logger_error_with_category("SIP", "pjsua_acc_add failed");
        pjsua_destroy();
        return -1;
    }

    /* Telephony only needs G.711 (PCMU/PCMA), which is also what Twilio uses.
     * Disable every other codec so the SDP stays small and the Pi stays cool. */
    {
        pjsua_codec_info ci[48];
        unsigned n = PJ_ARRAY_SIZE(ci);
        pj_str_t pcmu = S("PCMU");
        pj_str_t pcma = S("PCMA");
        if (pjsua_enum_codecs(ci, &n) == PJ_SUCCESS) {
            unsigned i;
            for (i = 0; i < n; i++)
                pjsua_codec_set_priority(&ci[i].codec_id, PJMEDIA_CODEC_PRIO_DISABLED);
        }
        pjsua_codec_set_priority(&pcmu, (pj_uint8_t)255);
        pjsua_codec_set_priority(&pcma, (pj_uint8_t)254);
    }

    g_started = 1;
    logger_info_with_category("SIP", "PJSUA started");
    return 0;
}

void pjsip_iface_stop(void) {
    if (!g_started) return;  /* never came up (e.g. SIP not configured) */
    thread_ensure();
    pjsua_destroy();
    g_started = 0;
    g_acc_id = PJSUA_INVALID_ID;
    g_call_id = PJSUA_INVALID_ID;
    g_registered = 0;
}

int pjsip_iface_call(const char *user) {
    char uri[256];
    pj_str_t puri;
    pj_status_t status;
    pjsua_call_id cid = PJSUA_INVALID_ID;

    if (!g_started || !user || g_acc_id == PJSUA_INVALID_ID || !g_domain[0]) {
        logger_error_with_category("SIP", "Cannot call: not ready");
        return -1;
    }
    thread_ensure();

    if (g_transport == PJSIP_IFACE_TRANSPORT_TLS)
        snprintf(uri, sizeof(uri), "sip:%s@%s;transport=tls", user, g_domain);
    else
        snprintf(uri, sizeof(uri), "sip:%s@%s", user, g_domain);

    puri = S(uri);
    logger_infof_with_category("SIP", "Dialing %s", uri);
    status = pjsua_call_make_call(g_acc_id, &puri, NULL, NULL, NULL, &cid);
    if (status != PJ_SUCCESS) {
        logger_errorf_with_category("SIP", "make_call failed (%d)", (int)status);
        return -1;
    }
    g_call_id = cid;
    return 0;
}

int pjsip_iface_answer(void) {
    if (!g_started) return -1;
    thread_ensure();
    if (g_call_id == PJSUA_INVALID_ID) {
        logger_warn_with_category("SIP", "No call to answer");
        return -1;
    }
    return (pjsua_call_answer(g_call_id, 200, NULL, NULL) == PJ_SUCCESS) ? 0 : -1;
}

void pjsip_iface_hangup(void) {
    if (!g_started) return;
    thread_ensure();
    if (g_call_id != PJSUA_INVALID_ID) {
        pjsua_call_hangup(g_call_id, 0, NULL, NULL);
    } else {
        /* Defensive: drop any stray calls. */
        pjsua_call_hangup_all();
    }
}

int pjsip_iface_send_dtmf(char key) {
    char digit[2];
    pj_str_t d;
    if (!g_started) return -1;
    thread_ensure();
    if (g_call_id == PJSUA_INVALID_ID) return -1;
    digit[0] = key;
    digit[1] = '\0';
    d = S(digit);
    return (pjsua_call_dial_dtmf(g_call_id, &d) == PJ_SUCCESS) ? 0 : -1;
}

int pjsip_iface_is_registered(void) {
    return g_registered;
}

void pjsip_iface_list_audio_devices(void) {
    unsigned count = 0;
    pjmedia_aud_dev_info info[64];
    unsigned i;

    if (!g_started) return;
    count = PJ_ARRAY_SIZE(info);
    if (pjsua_enum_aud_devs(info, &count) != PJ_SUCCESS) {
        logger_warn_with_category("SIP", "Could not enumerate audio devices");
        return;
    }
    for (i = 0; i < count; i++) {
        logger_infof_with_category("SIP", "Audio dev %u: %s (in=%u out=%u)",
            i, info[i].name, info[i].input_count, info[i].output_count);
    }
}

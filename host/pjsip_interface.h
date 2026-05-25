#ifndef PJSIP_INTERFACE_H
#define PJSIP_INTERFACE_H

/*
 * PJSIP (PJSUA C API) interface for the Millennium daemon.
 *
 * This header deliberately exposes ONLY plain C declarations — no PJSIP
 * headers — so that millennium_sdk.c (and anything else) can include it and
 * still compile on a developer Mac without pjproject installed. The actual
 * implementation in pjsip_interface.c includes <pjsua-lib/pjsua.h> and is
 * built only on the Pi (where pjproject is available), exactly like the old
 * baresip_interface split.
 *
 * Why PJSIP instead of embedding Baresip: Baresip is an application, not a
 * library, so embedding its internal API required running the daemon as the
 * ~/.baresip user, redirecting stdin, disabling its stdio module, relaxing
 * systemd hardening, and tracking its internal ABI. PJSUA is a purpose-built
 * embeddable C library: account config is programmatic (SIP credentials live
 * in /etc/millennium/daemon.conf, no separate accounts file), it owns its own
 * worker threads, and in-call DTMF is a first-class call (pjsua_call_dial_dtmf).
 */

#include <stddef.h>

/* Events delivered to the daemon via the registered callback. */
enum pjsip_iface_event {
    PJSIP_IFACE_REG_OK = 0,      /* SIP registration succeeded */
    PJSIP_IFACE_REG_FAIL,        /* SIP registration failed (text = reason) */
    PJSIP_IFACE_CALL_INCOMING,   /* an inbound call is ringing */
    PJSIP_IFACE_CALL_ESTABLISHED,/* a call became active (media flowing) */
    PJSIP_IFACE_CALL_CLOSED      /* the active call ended/disconnected */
};

/* Transport selection for the SIP signalling. */
#define PJSIP_IFACE_TRANSPORT_UDP 0
#define PJSIP_IFACE_TRANSPORT_TCP 1
#define PJSIP_IFACE_TRANSPORT_TLS 2

/* Callback invoked (on a PJSUA worker thread) for registration and call
 * events. `text` is a human-readable detail (e.g. a failure reason) or NULL. */
typedef void (*pjsip_iface_event_cb)(enum pjsip_iface_event ev,
                                     const char *text, void *arg);

/* SIP account configuration, supplied by the daemon from its config file.
 * All strings are borrowed for the duration of the pjsip_iface_start() call
 * (PJSUA copies what it needs), so the caller may free them afterwards. */
typedef struct {
    const char *id_uri;      /* AOR, e.g. "sip:+15551234567@ex.sip.twilio.com" */
    const char *reg_uri;     /* registrar, e.g. "sip:ex.sip.twilio.com" */
    const char *realm;       /* auth realm, or "*" to match any */
    const char *username;    /* auth username */
    const char *password;    /* auth password */
    const char *stun_server; /* e.g. "stun.l.google.com:19302", or NULL/"" */
    int transport;           /* PJSIP_IFACE_TRANSPORT_* */
    int local_port;          /* local SIP port, 0 = auto */
    /* ALSA device NAMES (matched against PJSUA's device list at startup), or
     * "" for PJSUA's auto default. Names are used rather than numeric indices
     * because the index order is not stable. The raw "hw:..." device is
     * stereo-only and will fail at mono; use a plug-wrapped device, e.g.
     * capture "plughw:CARD=Device,DEV=0", playback "out_right_solo". */
    const char *snd_capture;
    const char *snd_playback;
} pjsip_iface_account_t;

/* Bring up PJSUA: create, init, add a UDP/TCP/TLS transport, register the
 * account, and start. Registers `cb` for events. Returns 0 on success. */
int pjsip_iface_start(const pjsip_iface_account_t *acc,
                      pjsip_iface_event_cb cb, void *arg);

/* Tear everything down (hangs up calls, unregisters, destroys PJSUA). */
void pjsip_iface_stop(void);

/* Place an outbound call. `user` is the user part (e.g. "+15551234567");
 * the configured account domain is appended. Returns 0 if dialing started. */
int pjsip_iface_call(const char *user);

/* Answer the current inbound call (200 OK). Returns 0 on success. */
int pjsip_iface_answer(void);

/* Hang up the current call, if any. */
void pjsip_iface_hangup(void);

/* Send DTMF digit(s) on the current call via RFC 2833. Returns 0 on success,
 * -1 if there is no active call. Accepts 0-9, *, #, A-D. */
int pjsip_iface_send_dtmf(char key);

/* 1 if the account is currently registered, else 0. */
int pjsip_iface_is_registered(void);

/* Log the available PJSUA audio capture/playback devices (for diagnostics). */
void pjsip_iface_list_audio_devices(void);

#endif /* PJSIP_INTERFACE_H */

/*
 * PJSIP runtime smoke test.
 *
 * Exercises the real PJSUA lifecycle through pjsip_interface.c WITHOUT a SIP
 * peer or the phone hardware: create/init/transport/account/start, enumerate
 * audio devices, register a (foreign) calling thread, attempt a call to an
 * unreachable target, hang up, and tear everything down. It proves the
 * integration initializes and shuts down cleanly and that our PJSUA API usage
 * is correct at runtime — the parts we cannot check from the simulator.
 *
 * Registration to the dummy registrar is expected to FAIL; that is fine — we
 * only assert that PJSUA comes up and the lifecycle runs without crashing.
 *
 * Build/run (needs libpjproject):
 *   make pjsip-smoke && ./pjsip_smoke
 *
 * `make pjsip-smoke` uses `pkg-config --libs libpjproject`, which is complete
 * for a source build (the Pi, or Linux). On macOS via `brew install pjproject`
 * the generated .pc omits the bundled libsrtp/codec archives and the Apple
 * frameworks, so link those explicitly (see the bundled archives under
 * $(brew --prefix)/Cellar/pjproject/<ver>/lib plus -framework CoreAudio … ).
 */
#include "../pjsip_interface.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_reg_events = 0;
static int g_call_events = 0;

static void on_event(enum pjsip_iface_event ev, const char *text, void *arg) {
    (void)arg;
    switch (ev) {
    case PJSIP_IFACE_REG_OK:
        g_reg_events++; printf("  [event] REG_OK\n"); break;
    case PJSIP_IFACE_REG_FAIL:
        g_reg_events++; printf("  [event] REG_FAIL: %s\n", text ? text : ""); break;
    case PJSIP_IFACE_CALL_INCOMING:
        g_call_events++; printf("  [event] CALL_INCOMING\n"); break;
    case PJSIP_IFACE_CALL_ESTABLISHED:
        g_call_events++; printf("  [event] CALL_ESTABLISHED\n"); break;
    case PJSIP_IFACE_CALL_CLOSED:
        g_call_events++; printf("  [event] CALL_CLOSED: %s\n", text ? text : ""); break;
    }
}

int main(void) {
    pjsip_iface_account_t acc;
    int rc;

    printf("== PJSIP smoke test ==\n");

    memset(&acc, 0, sizeof(acc));
    acc.id_uri      = "sip:test@127.0.0.1";
    acc.reg_uri     = "sip:127.0.0.1";
    acc.realm       = "*";
    acc.username    = "test";
    acc.password    = "test";
    acc.stun_server = "";
    acc.transport   = PJSIP_IFACE_TRANSPORT_UDP;
    acc.local_port  = 0;       /* ephemeral, avoids clashing with port 5060 */
    acc.snd_capture_dev  = -1; /* PJSUA default device */
    acc.snd_playback_dev = -1;

    printf("[1] pjsip_iface_start...\n");
    rc = pjsip_iface_start(&acc, on_event, NULL);
    if (rc != 0) {
        printf("FAIL: pjsip_iface_start returned %d\n", rc);
        return 1;
    }
    printf("    OK (PJSUA up)\n");

    printf("[2] is_registered (expected 0 right after start): %d\n",
           pjsip_iface_is_registered());

    printf("[3] list audio devices...\n");
    pjsip_iface_list_audio_devices();

    /* Let registration attempt run (it will fail against 127.0.0.1). */
    sleep(2);

    printf("[4] place call to unreachable target (from this foreign thread)...\n");
    rc = pjsip_iface_call("+15551234567");
    printf("    pjsip_iface_call returned %d\n", rc);

    printf("[5] send DTMF (no active call -> expect -1): %d\n",
           pjsip_iface_send_dtmf('5'));

    sleep(1);

    printf("[6] hangup...\n");
    pjsip_iface_hangup();

    printf("[7] pjsip_iface_stop...\n");
    pjsip_iface_stop();

    printf("    OK (PJSUA down)\n");
    printf("== smoke test complete: reg_events=%d call_events=%d ==\n",
           g_reg_events, g_call_events);
    printf("RESULT: PASS (lifecycle ran without crashing)\n");
    return 0;
}

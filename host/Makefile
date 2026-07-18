CXXFLAGS=-g -O3 -std=c++17 -Wall -Wextra
# pjproject ships static .a libs; its ssl/crypto/srtp/codec deps live in the
# .pc Libs.private, so we need --static to pull them into the link.
LDFLAGS=-lpthread -latomic -lasound `pkg-config --libs --static libpjproject`

# C flags for C files. The codebase targets C89. We use -std=gnu89 (the C89
# language plus the GNU extensions that the ALSA and PJSIP system headers rely
# on, e.g. `inline`) because strict -std=c89 fails to parse those headers.
# -Wdeclaration-after-statement keeps OUR OWN code free of C99-style mixed
# declarations, so the source stays C89-conformant.
CFLAGS=-g -O3 -Wall -Wextra -std=gnu89 -Wdeclaration-after-statement -Werror

# Build version info
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_TIME := $(shell date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo "unknown")
VERSION_CFLAGS = -DGIT_HASH='"$(GIT_HASH)"' -DBUILD_TIME='"$(BUILD_TIME)"'

# Object files
config.o: config.c config.h
	gcc config.c -o config.o -c $(CFLAGS)

cli.o: cli.c cli.h
	gcc cli.c -o cli.o -c $(CFLAGS)

# C object files
clock_source.o: clock_source.c clock_source.h
	gcc clock_source.c -o clock_source.o -c $(CFLAGS)

daemon_state.o: daemon_state.c daemon_state.h clock_source.h
	gcc daemon_state.c -o daemon_state.o -c $(CFLAGS)

logger.o: logger.c logger.h
	gcc logger.c -o logger.o -c $(CFLAGS)

health_monitor.o: health_monitor.c health_monitor.h logger.h metrics.h
	gcc health_monitor.c -o health_monitor.o -c $(CFLAGS)

metrics.o: metrics.c metrics.h
	gcc metrics.c -o metrics.o -c $(CFLAGS)

call_metrics.o: call_metrics.c call_metrics.h metrics.h clock_source.h
	gcc call_metrics.c -o call_metrics.o -c $(CFLAGS)

metrics_server.o: metrics_server.c metrics_server.h metrics.h logger.h
	gcc metrics_server.c -o metrics_server.o -c $(CFLAGS)

websocket.o: websocket.c websocket.h
	gcc websocket.c -o websocket.o -c $(CFLAGS)

conn_queue.o: conn_queue.c conn_queue.h
	gcc conn_queue.c -o conn_queue.o -c $(CFLAGS)

web_server.o: web_server.c web_server.h conn_queue.h websocket.h config.h logger.h metrics.h health_monitor.h version.h updater.h
	gcc web_server.c -o web_server.o -c $(CFLAGS)

pjsip_interface.o: pjsip_interface.c pjsip_interface.h logger.h
	gcc pjsip_interface.c -o pjsip_interface.o -c $(CFLAGS) `pkg-config --cflags libpjproject`

events.o: events.c events.h
	gcc events.c -o events.o -c $(CFLAGS)

event_processor.o: event_processor.c event_processor.h events.h
	gcc event_processor.c -o event_processor.o -c $(CFLAGS)

millennium_sdk.o: millennium_sdk.c millennium_sdk.h events.h pjsip_interface.h config.h
	gcc millennium_sdk.c -o millennium_sdk.o -c $(CFLAGS)

state_persistence.o: state_persistence.c state_persistence.h daemon_state.h logger.h
	gcc state_persistence.c -o state_persistence.o -c $(CFLAGS)

display_manager.o: display_manager.c display_manager.h millennium_sdk.h
	gcc display_manager.c -o display_manager.o -c $(CFLAGS)

plugin_sdk.o: plugin_sdk.c plugin_sdk.h plugins.h display_manager.h audio_tones.h millennium_sdk.h clock_source.h logger.h daemon_state.h config.h
	gcc plugin_sdk.c -o plugin_sdk.o -c $(CFLAGS)

version.o: version.c version.h
	gcc version.c -o version.o -c $(CFLAGS) $(VERSION_CFLAGS)

wav.o: wav.c wav.h
	gcc wav.c -o wav.o -c $(CFLAGS)

audio_tones.o: audio_tones.c audio_tones.h wav.h logger.h
	gcc audio_tones.c -o audio_tones.o -c $(CFLAGS)

updater.o: updater.c updater.h version.h logger.h
	gcc updater.c -o updater.o -c $(CFLAGS)

daemon.o: daemon.c millennium_sdk.h events.h event_processor.h config.h logger.h health_monitor.h metrics.h metrics_server.h call_metrics.h web_server.h plugins.h state_persistence.h display_manager.h audio_tones.h
	gcc daemon.c -o daemon.o -c $(CFLAGS)

# Executables
plugins.o: plugins.c plugins.h
	gcc plugins.c -o plugins.o -c $(CFLAGS)

plugins/classic_phone.o: plugins/classic_phone.c plugins.h plugin_sdk.h audio_tones.h config.h
	gcc plugins/classic_phone.c -o plugins/classic_phone.o -c $(CFLAGS)

plugins/fortune_teller.o: plugins/fortune_teller.c plugins.h plugin_sdk.h
	gcc plugins/fortune_teller.c -o plugins/fortune_teller.o -c $(CFLAGS)

plugins/jukebox.o: plugins/jukebox.c plugins.h plugin_sdk.h
	gcc plugins/jukebox.c -o plugins/jukebox.o -c $(CFLAGS)

plugins/number_guess.o: plugins/number_guess.c plugins.h plugin_sdk.h config.h
	gcc plugins/number_guess.c -o plugins/number_guess.o -c $(CFLAGS)

plugins/simon.o: plugins/simon.c plugins.h plugin_sdk.h config.h
	gcc plugins/simon.c -o plugins/simon.o -c $(CFLAGS)

plugins/dial_a_joke.o: plugins/dial_a_joke.c plugins.h plugin_sdk.h config.h
	gcc plugins/dial_a_joke.c -o plugins/dial_a_joke.o -c $(CFLAGS)

plugins/trivia.o: plugins/trivia.c plugins.h plugin_sdk.h config.h
	gcc plugins/trivia.c -o plugins/trivia.o -c $(CFLAGS)

plugins/time_operator.o: plugins/time_operator.c plugins.h plugin_sdk.h
	gcc plugins/time_operator.c -o plugins/time_operator.o -c $(CFLAGS)

daemon: daemon.o daemon_state.o clock_source.o millennium_sdk.o pjsip_interface.o events.o event_processor.o config.o cli.o logger.o health_monitor.o metrics.o metrics_server.o call_metrics.o web_server.o websocket.o conn_queue.o plugins.o plugin_sdk.o plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o plugins/number_guess.o plugins/simon.o plugins/dial_a_joke.o plugins/trivia.o plugins/time_operator.o state_persistence.o display_manager.o version.o audio_tones.o wav.o updater.o
	gcc daemon.o daemon_state.o clock_source.o millennium_sdk.o pjsip_interface.o events.o event_processor.o config.o cli.o logger.o health_monitor.o metrics.o metrics_server.o call_metrics.o web_server.o websocket.o conn_queue.o plugins.o plugin_sdk.o plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o plugins/number_guess.o plugins/simon.o plugins/dial_a_joke.o plugins/trivia.o plugins/time_operator.o state_persistence.o display_manager.o version.o audio_tones.o wav.o updater.o -o daemon $(LDFLAGS) -lm

# Simulator object file
simulator.o: simulator.c millennium_sdk.h events.h config.h daemon_state.h logger.h metrics.h call_metrics.h plugins.h
	gcc simulator.c -o simulator.o -c $(CFLAGS)

# Simulator objects — no baresip, no web server, no daemon.o
SIM_OBJS = simulator.o daemon_state.o clock_source.o events.o event_processor.o config.o logger.o metrics.o call_metrics.o plugins.o plugin_sdk.o plugins/classic_phone.o plugins/fortune_teller.o plugins/number_guess.o plugins/simon.o plugins/dial_a_joke.o plugins/trivia.o plugins/time_operator.o state_persistence.o display_manager.o version.o

# Simulated time is portable: the simulator installs a clock source
# (clock_source.h) that the daemon/plugins read through, so no -Wl,--wrap hack.
SIM_LDFLAGS = -lpthread

simulator: $(SIM_OBJS)
	gcc $(SIM_OBJS) -o simulator $(SIM_LDFLAGS)

all: daemon

# Unit test binary
UNIT_TEST_OBJS = tests/unit_tests.o daemon_state.o clock_source.o events.o event_processor.o config.o cli.o logger.o metrics.o call_metrics.o health_monitor.o plugins.o plugin_sdk.o plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o plugins/number_guess.o plugins/simon.o plugins/dial_a_joke.o plugins/trivia.o plugins/time_operator.o state_persistence.o display_manager.o version.o wav.o updater.o conn_queue.o

tests/unit_tests.o: tests/unit_tests.c tests/test_framework.h config.h cli.h daemon_state.h plugins.h logger.h metrics.h call_metrics.h millennium_sdk.h updater.h state_persistence.h conn_queue.h health_monitor.h
	gcc tests/unit_tests.c -o tests/unit_tests.o -c $(CFLAGS) -I.

# Unit tests don't use time wrapping; link ALSA on Linux for jukebox
UNIT_LDFLAGS = -lpthread
ifeq ($(shell uname -s),Linux)
UNIT_LDFLAGS += -lasound -lm
endif

unit_tests: $(UNIT_TEST_OBJS)
	gcc $(UNIT_TEST_OBJS) -o unit_tests $(UNIT_LDFLAGS)

# Optional PJSIP runtime smoke test. Needs libpjproject on the dev box
# (macOS: `brew install pjproject`; Pi: built from source). Brings PJSUA up and
# down without a SIP peer or phone hardware — validates the PJSIP integration
# compiles and runs. Not part of `make test` (which must run anywhere).
pjsip-smoke: tests/pjsip_smoke.c pjsip_interface.c pjsip_interface.h logger.o
	gcc tests/pjsip_smoke.c pjsip_interface.c logger.o -o pjsip_smoke \
		$(CFLAGS) `pkg-config --cflags libpjproject` \
		`pkg-config --libs --static libpjproject` -lpthread -lm

# Run all scenario tests via the simulator
test: simulator unit_tests
	@echo "Running unit tests..."
	@./unit_tests
	@echo ""
	@echo "Running scenario tests..."
	@./simulator tests/*.scenario
	@echo ""

# Compile-check every daemon translation unit EXCEPT pjsip_interface.o (the only
# one needing libpjproject, which isn't available off the Pi). The simulator and
# unit-test builds stub the hardware layer, so daemon-only code — notably the
# ALSA paths in audio_tones.c, plus web_server/websocket/health_monitor — is
# never compiled by `make test`. This target catches type/syntax errors in that
# code on any Linux box with libasound2-dev (e.g. CI), short of a full link.
COMPILE_CHECK_OBJS = daemon.o daemon_state.o clock_source.o millennium_sdk.o events.o \
	event_processor.o config.o logger.o health_monitor.o metrics.o \
	metrics_server.o call_metrics.o web_server.o websocket.o conn_queue.o plugins.o plugin_sdk.o \
	plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o \
	plugins/number_guess.o plugins/simon.o plugins/dial_a_joke.o \
	plugins/trivia.o plugins/time_operator.o state_persistence.o display_manager.o version.o \
	audio_tones.o wav.o updater.o

.PHONY: compile-check
compile-check: $(COMPILE_CHECK_OBJS)
	@echo "compile-check OK: all daemon sources (except pjsip_interface) compiled"

clean:
	rm -rf *.o daemon simulator unit_tests pjsip_smoke plugins/*.o tests/*.o

install: daemon
	@systemctl --user stop daemon.service 2>/dev/null || true
	@systemctl --user disable daemon.service 2>/dev/null || true
	@rm -f $$HOME/.config/systemd/user/daemon.service 2>/dev/null || true
	@systemctl --user daemon-reload 2>/dev/null || true
	sudo systemctl stop daemon.service 2>/dev/null || true
	sudo mkdir -p /etc/millennium
	sudo mkdir -p /var/log/millennium
	sudo mkdir -p /var/lib/millennium
	@if command -v logname >/dev/null 2>&1; then sudo chown $$(logname):$$(logname) /var/lib/millennium 2>/dev/null || true; fi
	sudo cp daemon.conf.example /etc/millennium/daemon.conf
	sudo cp asoundrc.example /etc/asound.conf
	sudo cp daemon /usr/local/bin/millennium-daemon
	sudo mkdir -p /usr/local/share/millennium
	sudo mkdir -p /usr/local/share/millennium/audio
	sudo cp web_portal.html /usr/local/share/millennium/
	sudo cp systemd/daemon.service /etc/systemd/system/
	sudo mkdir -p /etc/systemd/system/daemon.service.d
	@printf '[Service]\nUser=%s\n' "$$(logname 2>/dev/null || whoami)" > daemon-override.conf.tmp && sudo cp daemon-override.conf.tmp /etc/systemd/system/daemon.service.d/override.conf; rm -f daemon-override.conf.tmp
	sudo systemctl daemon-reload
	sudo systemctl unmask daemon.service 2>/dev/null || true
	sudo systemctl enable daemon.service
	sudo systemctl start daemon.service

uninstall:
	sudo systemctl stop daemon.service
	sudo systemctl disable daemon.service
	sudo rm -f /usr/local/bin/millennium-daemon
	sudo rm -rf /usr/local/share/millennium
	sudo rm -rf /etc/systemd/system/daemon.service.d
	sudo rm -f /etc/systemd/system/daemon.service
	sudo systemctl daemon-reload

# API integration test (requires live daemon)
# Usage: make api-test [HOST=localhost]  e.g. make api-test HOST=192.168.86.145
HOST ?= localhost
api-test:
	@chmod +x tests/api_test.sh
	@HOST=$(HOST) ./tests/api_test.sh $(HOST)

# Run API test against the remote device (matzen@192.168.86.145)
# Override: make device-test HOST=other-ip
device-test:
	$(MAKE) api-test HOST=192.168.86.145

# Break/fuzz tests - verify JSON injection resistance, invalid input handling
break-test:
	@chmod +x tests/break_test.sh
	@HOST=$(HOST) ./tests/break_test.sh $(HOST)

# "The Operator" plugin smoke test against a live daemon over the web API. Drives
# the state machine end-to-end (greeting, eras, fares, 1999 drop, sealed refusal,
# paradox+repair). Does NOT cover audio/coins/card/hook — those need the physical
# phone; see host/OPERATOR_PLAYTEST.md. Usage: make operator-smoke [OP_HOST=<ip>]
# [RUN_DRIFT=1] (default host = the Pi).
OP_HOST ?= 192.168.86.152
operator-smoke:
	@chmod +x tests/operator_smoke.sh
	@RUN_DRIFT=$(RUN_DRIFT) ./tests/operator_smoke.sh $(OP_HOST)

# Regenerate The Operator's voice clips from audio/clips.manifest via ElevenLabs
# TTS. Needs ELEVENLABS_API_KEY in the environment and ffmpeg. Add DEPLOY=1 to
# also push the rebuilt WAVs to the Pi. See host/AUDIO_CLIPS.md.
regen-clips:
	@cd audio && ./regen_clips.sh $(if $(DEPLOY),--deploy,)

# ThreadSanitizer check for the serial event queue's lock (mirrors
# millennium_sdk.c). Needs a TSan-capable toolchain (clang/gcc on x86-64 or
# arm64; NOT the 32-bit Pi). The locked build must be race-free; the -DNO_LOCK
# build demonstrates the pre-fix race. Not part of `make test`.
TSAN_CC ?= clang
tsan-queue:
	@$(TSAN_CC) -fsanitize=thread -g -O1 tests/tsan_event_queue.c -o /tmp/tsan_queue_lock
	@echo "[locked] (expect: no races, prints done)"; /tmp/tsan_queue_lock
	@$(TSAN_CC) -fsanitize=thread -g -O1 -DNO_LOCK tests/tsan_event_queue.c -o /tmp/tsan_queue_nolock
	@echo "[no-lock] (expect: ThreadSanitizer data race warnings)"; /tmp/tsan_queue_nolock || true

# CBMC memory-safety proof of the serial event-buffer parser (mirrors
# millennium_sdk.c). Proves no OOB/overflow/leak for any serial input up to the
# modeled length. Needs cbmc installed; takes ~2 min. Not part of `make test`.
CBMC ?= cbmc
cbmc-parser:
	$(CBMC) tests/cbmc_serial_parser.c --function verify_parser \
	  --bounds-check --pointer-check --pointer-overflow-check --conversion-check \
	  --memory-leak-check --unwind 37 --unwinding-assertions

# CBMC memory-safety proofs of the other untrusted-input parsers (wav.c, the
# websocket frame decoder, config_trim). Run --32 to match the Pi. Needs cbmc.
CBMC_MS = --32 --bounds-check --pointer-check --pointer-overflow-check --memory-leak-check --unwinding-assertions
cbmc-parsers:
	$(CBMC) tests/cbmc_parsers.c wav.c --function verify_wav        $(CBMC_MS) --unwind 12
	$(CBMC) tests/cbmc_parsers.c        --function verify_ws_frame   $(CBMC_MS) --unwind 12
	$(CBMC) tests/cbmc_parsers.c        --function verify_config_trim $(CBMC_MS) --unwind 20

# SPIN model-check of the Pi<->Arduino display-command wire protocol
# (tests/protocol.pml, mirrors display.ino's framing). Proves overrun-freedom,
# deadlock-freedom, and full-consumption (self-resync). Needs spin; not in `make test`.
SPIN ?= spin
model-check:
	@tmp=$$(mktemp -d); \
	 grep -v '^ltl ' tests/protocol.pml > $$tmp/safety.pml; cp tests/protocol.pml $$tmp/live.pml; \
	 echo "== safety: overrun-freedom + deadlock-freedom =="; \
	 ( cd $$tmp && $(SPIN) -run -safety -m200000 safety.pml ) | grep -iE "assertion viol|invalid end|errors:"; \
	 echo "== liveness: full-consumption []<> at_idle =="; \
	 ( cd $$tmp && $(SPIN) -run -ltl progress -m200000 live.pml ) | grep -iE "acceptance|never claim|errors:"; \
	 rm -rf $$tmp

# SPIN model-check of The Operator game (tests/operator_game.pml, mirrors
# plugins/time_operator.c). Proves no dead-ends, bounded pieces, and no soft-lock
# (WIN reachable from every state, AG EF win). Needs spin; not in `make test`.
game-check:
	@tmp=$$(mktemp -d); \
	 grep -v '^ltl ' tests/operator_game.pml > $$tmp/safety.pml; cp tests/operator_game.pml $$tmp/g.pml; \
	 echo "== no dead-ends =="; \
	 ( cd $$tmp && $(SPIN) -run -safety -m300000 safety.pml ) | grep -iE "invalid end|errors:"; \
	 echo "== bounded pieces ([] pieces<=3) =="; \
	 ( cd $$tmp && $(SPIN) -run -ltl pieces_ok -m300000 g.pml ) | grep -iE "errors:"; \
	 echo "== no soft-lock (AG EF win) =="; \
	 ( cd $$tmp && $(SPIN) -run -ltl can_win -f -m1000000 g.pml ) | grep -iE "acceptance|errors:"; \
	 rm -rf $$tmp

# CBMC of the daemon phone state machine (tests/cbmc_state_machine.c): proves the
# state+balance invariants AND that simulator.c and daemon.c implement the same
# transitions (catches sim/daemon drift). Needs cbmc.
state-check:
	$(CBMC) tests/cbmc_state_machine.c --function verify_invariants
	$(CBMC) tests/cbmc_state_machine.c --function verify_equiv

# TLC model-check of concurrent event ordering (tests/EventOrdering.tla).
# Where state-check verifies ONE transition sequentially, this checks every
# interleaving of the two racing producers -- the serial/Arduino path and the
# PJSUA worker thread -- through the single-consumer main loop. Formalizes the
# hand-written case analysis in docs/EVENT_ORDERING.md (#100).
#
# Needs tla2tools.jar (https://github.com/tlaplus/tlaplus/releases) and a JRE:
#   make tla-check TLA_TOOLS=/path/to/tla2tools.jar
# Not part of `make test`; takes ~25s.
TLA_TOOLS ?= tla2tools.jar
TLC = java -XX:+UseParallelGC -cp $(TLA_TOOLS) tlc2.TLC

tla-check:
	cd tests && $(TLC) -config EventOrdering.cfg EventOrdering.tla

# The two invariants that currently FAIL, both from the unguarded
# EVENT_CALL_STATE_ACTIVE arm at daemon.c:387. Expected to report a violation
# until that arm is guarded -- see tests/EventOrderingRace.cfg for the traces.
tla-check-race:
	cd tests && $(TLC) -config EventOrderingRace.cfg EventOrdering.tla

.PHONY: all clean install uninstall test unit_tests api-test device-test break-test operator-smoke regen-clips tsan-queue cbmc-parser cbmc-parsers model-check game-check state-check tla-check tla-check-race pjsip-smoke

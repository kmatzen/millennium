CXXFLAGS=-g -O3 -std=c++17 -Wall -Wextra
LDFLAGS=-lpthread -latomic -lasound `pkg-config libre --libs` `pkg-config libbaresip --libs`

# C flags for C files
CFLAGS=-g -O3 -Wall -Wextra -std=c89
C99_CFLAGS=-g -O3 -Wall -Wextra -std=c99

# Object files
config.o: config.c config.h
	gcc config.c -o config.o -c $(CFLAGS)

# C object files
daemon_state.o: daemon_state.c daemon_state.h
	gcc daemon_state.c -o daemon_state.o -c $(CFLAGS)

logger.o: logger.c logger.h
	gcc logger.c -o logger.o -c $(CFLAGS)

health_monitor.o: health_monitor.c health_monitor.h logger.h
	gcc health_monitor.c -o health_monitor.o -c $(CFLAGS) -lpthread

metrics.o: metrics.c metrics.h
	gcc metrics.c -o metrics.o -c $(CFLAGS)

metrics_server.o: metrics_server.c metrics_server.h metrics.h logger.h
	gcc metrics_server.c -o metrics_server.o -c $(CFLAGS) -lpthread

web_server.o: web_server.c web_server.h config.h logger.h metrics.h health_monitor.h
	gcc web_server.c -o web_server.o -c $(CFLAGS)

baresip_interface.o: baresip_interface.c baresip_interface.h
	gcc baresip_interface.c -o baresip_interface.o -c $(C99_CFLAGS) `pkg-config --cflags libre libbaresip`

events.o: events.c events.h
	gcc events.c -o events.o -c $(CFLAGS)

event_processor.o: event_processor.c event_processor.h events.h
	gcc event_processor.c -o event_processor.o -c $(CFLAGS)

millennium_sdk.o: millennium_sdk.c millennium_sdk.h events.h baresip_interface.h
	gcc millennium_sdk.c -o millennium_sdk.o -c $(CFLAGS)

state_persistence.o: state_persistence.c state_persistence.h daemon_state.h logger.h
	gcc state_persistence.c -o state_persistence.o -c $(CFLAGS)

daemon.o: daemon.c millennium_sdk.h events.h event_processor.h config.h logger.h health_monitor.h metrics.h metrics_server.h web_server.h plugins.h state_persistence.h
	gcc daemon.c -o daemon.o -c $(CFLAGS)

# Executables
plugins.o: plugins.c plugins.h
	gcc plugins.c -o plugins.o -c $(CFLAGS)

plugins/classic_phone.o: plugins/classic_phone.c plugins.h
	gcc plugins/classic_phone.c -o plugins/classic_phone.o -c $(CFLAGS)

plugins/fortune_teller.o: plugins/fortune_teller.c plugins.h
	gcc plugins/fortune_teller.c -o plugins/fortune_teller.o -c $(CFLAGS)

plugins/jukebox.o: plugins/jukebox.c plugins.h
	gcc plugins/jukebox.c -o plugins/jukebox.o -c $(C99_CFLAGS) -lpthread

daemon: daemon.o daemon_state.o millennium_sdk.o baresip_interface.o events.o event_processor.o config.o logger.o health_monitor.o metrics.o metrics_server.o web_server.o plugins.o plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o state_persistence.o
	gcc daemon.o daemon_state.o millennium_sdk.o baresip_interface.o events.o event_processor.o config.o logger.o health_monitor.o metrics.o metrics_server.o web_server.o plugins.o plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o state_persistence.o -o daemon $(LDFLAGS)

# Simulator object file (uses C99 for mixed declarations)
simulator.o: simulator.c millennium_sdk.h events.h config.h daemon_state.h logger.h metrics.h plugins.h
	gcc simulator.c -o simulator.o -c $(C99_CFLAGS)

# Simulator objects — no baresip, no web server, no daemon.o
SIM_OBJS = simulator.o daemon_state.o events.o event_processor.o config.o logger.o metrics.o plugins.o plugins/classic_phone.o plugins/fortune_teller.o state_persistence.o

# On Linux, wrap time() for simulated time; elsewhere use real time
SIM_LDFLAGS = -lpthread
ifeq ($(shell uname -s),Linux)
SIM_LDFLAGS += -Wl,--wrap=time
endif

simulator: $(SIM_OBJS)
	gcc $(SIM_OBJS) -o simulator $(SIM_LDFLAGS)

all: daemon

# Unit test binary
UNIT_TEST_OBJS = tests/unit_tests.o daemon_state.o events.o event_processor.o config.o logger.o metrics.o plugins.o plugins/classic_phone.o plugins/fortune_teller.o plugins/jukebox.o state_persistence.o

tests/unit_tests.o: tests/unit_tests.c tests/test_framework.h config.h daemon_state.h plugins.h logger.h metrics.h millennium_sdk.h
	gcc tests/unit_tests.c -o tests/unit_tests.o -c $(C99_CFLAGS) -I.

unit_tests: $(UNIT_TEST_OBJS)
	gcc $(UNIT_TEST_OBJS) -o unit_tests $(SIM_LDFLAGS)

# Run all scenario tests via the simulator
test: simulator unit_tests
	@echo "Running unit tests..."
	@./unit_tests
	@echo ""
	@echo "Running scenario tests..."
	@./simulator tests/*.scenario
	@echo ""

clean:
	rm -rf *.o daemon simulator unit_tests plugins/*.o tests/*.o

install: daemon
	sudo mkdir -p /etc/millennium
	sudo mkdir -p /var/log/millennium
	sudo cp daemon.conf.example /etc/millennium/daemon.conf
	sudo cp daemon /usr/local/bin/millennium-daemon
	sudo cp systemd/daemon.service /etc/systemd/system/
	sudo systemctl daemon-reload
	sudo systemctl enable millennium-daemon.service

uninstall:
	sudo systemctl stop millennium-daemon.service
	sudo systemctl disable millennium-daemon.service
	sudo rm -f /usr/local/bin/millennium-daemon
	sudo rm -f /etc/systemd/system/millennium-daemon.service
	sudo systemctl daemon-reload

.PHONY: all clean install uninstall test unit_tests

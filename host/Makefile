CXXFLAGS=-g -O3 -std=c++17 -Wall -Wextra `pkg-config libbaresip --cflags` `pkg-config libre --cflags`
LDFLAGS=-lpthread -latomic `pkg-config libre --libs` `pkg-config libbaresip --libs`

# C flags for C files
CFLAGS=-g -O3 -Wall -Wextra -std=c89

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

web_server.o: web_server.cpp web_server.h config.h logger.h metrics.h health_monitor.h
	g++ web_server.cpp -o web_server.o -c $(CXXFLAGS)

test_framework.o: test_framework.cpp test_framework.h
	g++ test_framework.cpp -o test_framework.o -c $(CXXFLAGS)

millennium_sdk.o: millennium_sdk.cpp millennium_sdk.h
	g++ millennium_sdk.cpp -o millennium_sdk.o -c $(CXXFLAGS)

daemon.o: daemon.cpp millennium_sdk.h config.h logger.h health_monitor.h metrics.h metrics_server.h web_server.h
	g++ daemon.cpp -o daemon.o -c $(CXXFLAGS)

tests.o: tests.cpp test_framework.h config.h logger.h metrics.h
	g++ tests.cpp -o tests.o -c $(CXXFLAGS)

# Executables
daemon: daemon.o daemon_state.o millennium_sdk.o config.o logger.o health_monitor.o metrics.o metrics_server.o web_server.o
	gcc daemon.o daemon_state.o millennium_sdk.o config.o logger.o health_monitor.o metrics.o metrics_server.o web_server.o -o daemon $(LDFLAGS) -lstdc++

tests: tests.o test_framework.o config.o logger.o metrics.o
	gcc tests.o test_framework.o config.o logger.o metrics.o -o tests $(LDFLAGS) -lstdc++

metrics_viewer.o: metrics_viewer.cpp config.h logger.h metrics.h
	g++ metrics_viewer.cpp -o metrics_viewer.o -c $(CXXFLAGS)

metrics_viewer: metrics_viewer.o config.o logger.o metrics.o
	gcc metrics_viewer.o config.o logger.o metrics.o -o metrics_viewer $(LDFLAGS) -lstdc++

all: daemon tests metrics_viewer

clean:
	rm -rf *.o daemon tests metrics_viewer test_daemon_state

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

test: tests
	./tests

# C test executable
test_daemon_state: test_daemon_state.c daemon_state.o
	gcc test_daemon_state.c daemon_state.o -o test_daemon_state $(CFLAGS)

test_c: test_daemon_state
	./test_daemon_state

.PHONY: all clean install uninstall test test_c

CXXFLAGS=-g -O3 -std=c++17 -Wall -Wextra `pkg-config libbaresip --cflags` `pkg-config libre --cflags`
LDFLAGS=-lpthread -latomic `pkg-config libre --libs` `pkg-config libbaresip --libs`

# Object files
config.o: config.cpp config.h
	g++ config.cpp -o config.o -c $(CXXFLAGS)

logger.o: logger.cpp logger.h
	g++ logger.cpp -o logger.o -c $(CXXFLAGS)

health_monitor.o: health_monitor.cpp health_monitor.h logger.h
	g++ health_monitor.cpp -o health_monitor.o -c $(CXXFLAGS)

metrics.o: metrics.cpp metrics.h
	g++ metrics.cpp -o metrics.o -c $(CXXFLAGS)

test_framework.o: test_framework.cpp test_framework.h
	g++ test_framework.cpp -o test_framework.o -c $(CXXFLAGS)

millennium_sdk.o: millennium_sdk.cpp millennium_sdk.h
	g++ millennium_sdk.cpp -o millennium_sdk.o -c $(CXXFLAGS)

daemon.o: daemon.cpp millennium_sdk.h
	g++ daemon.cpp -o daemon.o -c $(CXXFLAGS)

daemon_improved.o: daemon_improved.cpp millennium_sdk.h config.h logger.h health_monitor.h metrics.h
	g++ daemon_improved.cpp -o daemon_improved.o -c $(CXXFLAGS)

tests.o: tests.cpp test_framework.h config.h logger.h metrics.h
	g++ tests.cpp -o tests.o -c $(CXXFLAGS)

# Executables
daemon: daemon.o millennium_sdk.o
	g++ daemon.o millennium_sdk.o -o daemon $(LDFLAGS)

daemon_improved: daemon_improved.o millennium_sdk.o config.o logger.o health_monitor.o metrics.o
	g++ daemon_improved.o millennium_sdk.o config.o logger.o health_monitor.o metrics.o -o daemon_improved $(LDFLAGS)

tests: tests.o test_framework.o config.o logger.o metrics.o
	g++ tests.o test_framework.o config.o logger.o metrics.o -o tests $(LDFLAGS)

metrics_viewer.o: metrics_viewer.cpp config.h logger.h metrics.h
	g++ metrics_viewer.cpp -o metrics_viewer.o -c $(CXXFLAGS)

metrics_viewer: metrics_viewer.o config.o logger.o metrics.o
	g++ metrics_viewer.o config.o logger.o metrics.o -o metrics_viewer $(LDFLAGS)

all: daemon daemon_improved tests metrics_viewer

clean:
	rm -rf *.o daemon daemon_improved tests metrics_viewer

install: daemon_improved
	sudo mkdir -p /etc/millennium
	sudo mkdir -p /var/log/millennium
	sudo cp daemon.conf.example /etc/millennium/daemon.conf
	sudo cp daemon_improved /usr/local/bin/millennium-daemon
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

.PHONY: all clean install uninstall test

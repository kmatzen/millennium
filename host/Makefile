CXXFLAGS=-g -O3 `pkg-config libbaresip --cflags` `pkg-config libre --cflags`
LDFLAGS=-lpthread `pkg-config libre --libs` `pkg-config libbaresip --libs`

millennium_sdk.o: millennium_sdk.cpp millennium_sdk.h
	g++ millennium_sdk.cpp -o millennium_sdk.o -c $(CXXFLAGS)

daemon.o: daemon.cpp millennium_sdk.h
	g++ daemon.cpp -o daemon.o -c $(CXXFLAGS)

daemon: daemon.o millennium_sdk.o
	g++ daemon.o millennium_sdk.o -o daemon $(LDFLAGS)


all: daemon

clean:
	rm -rf *.o daemon

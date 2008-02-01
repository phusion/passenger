APXS=apxs2
APACHECTL=apache2ctl
CXX=g++
CXXFLAGS=-Wall -fPIC -g -DPASSENGER_DEBUG `pkg-config --cflags apr-1 apr-util-1` `$(APXS) -q CFLAGS` -I`$(APXS) -q INCLUDEDIR`
OBJECTS=Configuration.o Hooks.o DispatcherBucket.o Utils.o
LIBTOOL_OBJECTS=Configuration.o,Hooks.o,DispatcherBucket.o,Utils.o

.PHONY: all install clean start restart stop

all: $(OBJECTS)
	$(APXS) -c mod_rails.c -Wc,$(LIBTOOL_OBJECTS) -Wl,-lstdc++,-lboost_thread

install: $(OBJECTS)
	$(APXS) -i -c mod_rails.c -Wc,$(LIBTOOL_OBJECTS) -Wl,-lstdc++,-lboost_thread

Configuration.o: Configuration.cpp
	$(CXX) $(CXXFLAGS) -c Configuration.cpp

Hooks.o: Hooks.cpp Hooks.h Types.h DispatcherBucket.h ApplicationPool.h SpawnManager.h Exceptions.h Application.h MessageChannel.h Utils.h
	$(CXX) $(CXXFLAGS) -c Hooks.cpp

DispatcherBucket.o: DispatcherBucket.h DispatcherBucket.cpp Utils.h
	$(CXX) $(CXXFLAGS) -c DispatcherBucket.cpp

Utils.o: Utils.cpp Utils.h
	$(CXX) $(CXXFLAGS) -c Utils.cpp

clean:
	rm -rf *.o mod_rails.lo mod_rails.slo mod_rails.la .libs

start:
	$(APACHECTL) start
restart:
	$(APACHECTL) restart
stop:
	$(APACHECTL) stop

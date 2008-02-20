APXS=apxs2
APACHECTL=apache2ctl
CXX=g++
CXXFLAGS=-Wall -fPIC -g -DPASSENGER_DEBUG `pkg-config --cflags apr-1 apr-util-1` `$(APXS) -q CFLAGS` -I`$(APXS) -q INCLUDEDIR` -I/usr/local/include -I..
OBJECTS=Configuration.o Hooks.o Utils.o
LIBTOOL_OBJECTS=Configuration.o,Hooks.o,Utils.o
LIBBOOST_THREAD=../boost/src/libboost_thread.a

.PHONY: all install clean start restart stop

all: $(OBJECTS) $(LIBBOOST_THREAD)
	$(APXS) -c mod_rails.c -Wc,$(LIBTOOL_OBJECTS) -I/usr/local/include -L/usr/local/lib -Wl,-lstdc++,-lpthread,$(LIBBOOST_THREAD)

install: $(OBJECTS) $(LIBBOOST_THREAD)
	$(APXS) -i -c mod_rails.c -Wc,$(LIBTOOL_OBJECTS) -I/usr/local/include -L/usr/local/lib -Wl,-lstdc++,-lpthread,$(LIBBOOST_THREAD)

$(LIBBOOST_THREAD): ../boost/src/*.cpp
	make -C ../boost/src

Configuration.o: Configuration.cpp
	$(CXX) $(CXXFLAGS) -c Configuration.cpp

Hooks.o: Hooks.cpp Hooks.h Configuration.h ApplicationPool.h ApplicationPoolClientServer.h SpawnManager.h Exceptions.h Application.h MessageChannel.h Utils.h
	$(CXX) $(CXXFLAGS) -c Hooks.cpp

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

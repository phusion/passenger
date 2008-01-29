APXS=apxs2
APACHECTL=apache2ctl
CXX=g++
CXXFLAGS=-Wall -fPIC -g `pkg-config --cflags apr-util-1`
OBJECTS=DispatcherBucket.o

.PHONY: all install clean start restart stop

all: $(OBJECTS)
	CC=g++ $(APXS) -c mod_rails.c -Wc,DispatcherBucket.o -Wl,-lstdc++

install: $(OBJECTS)
	CC=g++ $(APXS) -i -c mod_rails.c -Wc,DispatcherBucket.o -Wl,-lstdc++

DispatcherBucket.o: DispatcherBucket.cpp
	$(CXX) $(CXXFLAGS) -c DispatcherBucket.cpp

clean:
	rm -rf *.o mod_rails.lo mod_rails.slo mod_rails.la .libs

start:
	$(APACHECTL) start
restart:
	$(APACHECTL) restart
stop:
	$(APACHECTL) stop

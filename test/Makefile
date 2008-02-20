RSPEC=spec
CXX=g++
CXXFLAGS=-Isupport -DVALGRIND_FRIENDLY -g -Wall `pkg-config --cflags apr-1 apr-util-1`
AP2_FLAGS=-I../ext/apache2 -I../ext
AP2_OBJECTS=CxxTestMain.o MessageChannelTest.o SpawnManagerTest.o ApplicationPoolClientServerTest.o \
	StandardApplicationPoolTest.o
LIBBOOST_THREAD=../ext/boost/src/libboost_thread.a

.PHONY: test_all clean

test_all: Apache2ModuleTests
	./Apache2ModuleTests

Apache2ModuleTests: $(AP2_OBJECTS) $(LIBBOOST_THREAD)
	$(CXX) $(AP2_OBJECTS) -o Apache2ModuleTests `pkg-config --libs apr-1 apr-util-1` $(LIBBOOST_THREAD) -lpthread

$(LIBBOOST_THREAD): ../ext/boost/src/*.cpp
	make -C ../ext/boost/src

CxxTestMain.o: CxxTestMain.cpp
	$(CXX) $(CXXFLAGS) -c CxxTestMain.cpp

MessageChannelTest.o: MessageChannelTest.cpp ../ext/apache2/MessageChannel.h
	$(CXX) $(CXXFLAGS) $(AP2_FLAGS) -c MessageChannelTest.cpp

SpawnManagerTest.o: SpawnManagerTest.cpp ../ext/apache2/SpawnManager.h
	$(CXX) $(CXXFLAGS) $(AP2_FLAGS) -c SpawnManagerTest.cpp

ApplicationPoolClientServerTest.o: ApplicationPoolClientServerTest.cpp ApplicationPoolTestTemplate.cpp ../ext/apache2/ApplicationPoolClientServer.h ../ext/apache2/ApplicationPool.h
	$(CXX) $(CXXFLAGS) $(AP2_FLAGS) -c ApplicationPoolClientServerTest.cpp

StandardApplicationPoolTest.o: StandardApplicationPoolTest.cpp ApplicationPoolTestTemplate.cpp ../ext/apache2/ApplicationPool.h
	$(CXX) $(CXXFLAGS) $(AP2_FLAGS) -c StandardApplicationPoolTest.cpp

clean:
	rm -f Apache2ModuleTests *.o

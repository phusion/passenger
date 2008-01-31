CXX=g++
CXXFLAGS=-Isupport -g -Wall `pkg-config --cflags apr-1 apr-util-1`
AP2_FLAGS=-I../ext/apache2
AP2_OBJECTS=CxxTestMain.o MessageChannelTest.o

.PHONY: test_all clean

test_all: Apache2ModuleTests
	./Apache2ModuleTests

Apache2ModuleTests: $(AP2_OBJECTS)
	$(CXX) $(AP2_OBJECTS) -o Apache2ModuleTests `pkg-config --libs apr-1 apr-util-1`

CxxTestMain.o: CxxTestMain.cpp
	$(CXX) $(CXXFLAGS) -c CxxTestMain.cpp

SpawnManagerTest.o: SpawnManagerTest.cpp ../ext/apache2/SpawnManager.h
	$(CXX) $(CXXFLAGS) $(AP2_FLAGS) -c SpawnManagerTest.cpp

MessageChannelTest.o: MessageChannelTest.cpp ../ext/apache2/MessageChannel.h
	$(CXX) $(CXXFLAGS) $(AP2_FLAGS) -c MessageChannelTest.cpp

clean:
	rm -f Apache2ModuleTests *.o
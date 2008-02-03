CXX=g++
CXXFLAGS=-Wall -g -I../ext/apache2 `pkg-config --cflags apr-1`

DummyRequestHandler: DummyRequestHandler.cpp ../ext/apache2/MessageChannel.h
	$(CXX) $(CXXFLAGS) -o DummyRequestHandler DummyRequestHandler.cpp

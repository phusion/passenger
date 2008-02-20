CXX=g++
# Note: NDEBUG *must* be defined! boost::thread use assert() to check whether
# the pthread functions return an error. Because of the way Passenger uses
# processes, sometimes pthread errors will occur. These errors are harmless
# and should be ignored. Defining NDEBUG guarantees that boost::thread() will
# not abort if such an error occured.
CXXFLAGS=-I../.. -DNDEBUG

.PHONY: clean

libboost_thread.a: *.cpp
	$(CXX) $(CXXFLAGS) -c *.cpp
	ar cru libboost_thread.a *.o

clean:
	rm -f libboost_thread.a


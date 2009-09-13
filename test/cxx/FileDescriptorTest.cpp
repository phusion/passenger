#include "TestSupport.h"
#include "FileDescriptor.h"
#include <cerrno>

using namespace Passenger;

namespace tut {
	struct FileDescriptorTest {
		int pipes[2];
		
		FileDescriptorTest() {
			pipe(pipes);
		}
		
		~FileDescriptorTest() {
			if (pipes[0] != -1) {
				close(pipes[0]);
			}
			if (pipes[1] != -1) {
				close(pipes[1]);
			}
		}
	};
	
	DEFINE_TEST_GROUP(FileDescriptorTest);
	
	TEST_METHOD(1) {
		// Test constructors.
		FileDescriptor f;
		ensure_equals("An empty FileDescriptor has value -1",
			f, -1);
		
		int fd = pipes[0];
		pipes[0] = -1;
		f = FileDescriptor(fd);
		ensure_equals("FileDescriptor takes the value of its constructor argument",
			f, fd);
	}
	
	TEST_METHOD(2) {
		// It closes the underlying file descriptor when the last
		// instance is destroyed.
		int reader = pipes[0];
		pipes[0] = -1;
		{
			FileDescriptor f(reader);
			{
				FileDescriptor f2(f);
			}
			ensure("File descriptor is not closed if there are still live copies",
				write(pipes[1], "x", 1) != -1);
		}
		ensure("File descriptor is closed if the last live copy is dead",
			write(pipes[1], "x", 1) == -1);
	}
	
	TEST_METHOD(3) {
		// Calling close() will close the underlying file descriptor for all instances.
		int reader = pipes[0];
		pipes[0] = -1;
		
		FileDescriptor f(reader);
		FileDescriptor f2(f);
		f.close();
		ensure_equals(f, -1);
		ensure_equals(f2, -1);
		ensure(write(pipes[1], "x", 1) == -1);
	}
}

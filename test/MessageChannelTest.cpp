#include "tut.h"
#include "MessageChannel.h"

#include <cstring>
#include <cstdio>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct MessageChannelTest {
		MessageChannel reader, writer;
		int p[2];

		MessageChannelTest() {
			pipe(p);
			reader = MessageChannel(p[0]);
			writer = MessageChannel(p[1]);
		}
		
		~MessageChannelTest() {
			close(p[0]);
			close(p[1]);
		}
	};

	DEFINE_TEST_GROUP(MessageChannelTest);

	TEST_METHOD(1) {
		// read() should be able to parse a message constructed by write(name, ...).
		vector<string> args;
		
		writer.write("hello", "world", "!", NULL);
		ensure("End of file has not been reached", reader.read(args));
		ensure_equals("read() returns the same number of arguments as passed to write()", args.size(), 3u);
		ensure_equals(args[0], "hello");
		ensure_equals(args[1], "world");
		ensure_equals(args[2], "!");
	}
	
	TEST_METHOD(2) {
		// read() should be able to parse a message constructed by write(list).
		list<string> input;
		vector<string> output;
		
		input.push_back("hello");
		input.push_back("world");
		input.push_back("!");
		writer.write(input);
		ensure("End of file has not been reached", reader.read(output));
		ensure_equals("read() returns the same number of arguments as passed to write()", input.size(), output.size());
		
		list<string>::const_iterator it;
		vector<string>::const_iterator it2;
		for (it = input.begin(), it2 = output.begin(); it != input.end(); it++, it2++) {
			ensure_equals(*it, *it2);
		}
	}
	
	TEST_METHOD(3) {
		// write() should be able to properly serialize arguments that contain whitespace.
		vector<string> args;
		writer.write("hello", "world with whitespaces", "!!!", NULL);
		ensure("End of file has not been reached", reader.read(args));
		ensure_equals(args[1], "world with whitespaces");
	}
	
	TEST_METHOD(4) {
		// read() should be able to read messages constructed by the Ruby implementation.
		// write() should be able to construct messages that can be read by the Ruby implementation.
		// Multiple read() and write() calls should work (i.e. the MessageChannel should have stream properties).
		// End of file should be properly detected.
		int p1[2], p2[2];
		pid_t pid;
		
		pipe(p1);
		pipe(p2);
		pid = fork();
		if (pid == 0) {
			dup2(p1[0], 0);
			dup2(p2[1], 1);
			close(p1[0]);
			close(p1[1]);
			close(p2[0]);
			close(p2[1]);
			execlp("ruby", "ruby", "./stub/message_channel.rb", NULL);
			perror("Cannot execute ruby");
			_exit(1);
		} else {
			MessageChannel input(p1[1]);
			MessageChannel output(p2[0]);
			close(p1[0]);
			close(p2[1]);
			
			input.write("hello", "my beautiful", "world", NULL);
			input.write("you have", "not enough", "minerals", NULL);
			input.close();
			
			vector<string> message1, message2, message3;
			ensure("End of stream has not been reached (1)", output.read(message1));
			ensure("End of stream has not been reached (2)", output.read(message2));
			ensure("End of file has been reached", !output.read(message3));
			output.close();
			waitpid(pid, NULL, 0);
			
			ensure_equals("First message is correctly transformed by the mock object",
				message1.size(), 4u);
			ensure_equals(message1[0], "hello");
			ensure_equals(message1[1], "my beautiful");
			ensure_equals(message1[2], "world");
			ensure_equals(message1[3], "!!");
			
			ensure_equals("Second message is correctly transformed by the mock object",
				message2.size(), 4u);
			ensure_equals(message2[0], "you have");
			ensure_equals(message2[1], "not enough");
			ensure_equals(message2[2], "minerals");
			ensure_equals(message2[3], "??");
		}
	}
	
	TEST_METHOD(6) {
		// write(name) should generate a correct message even if there are no additional arguments.
		writer.write("hello", NULL);
		vector<string> args;
		reader.read(args);
		ensure_equals(args.size(), 1u);
		ensure_equals(args[0], "hello");
	}
	
	TEST_METHOD(7) {
		// writeFileDescriptor() and receiveFileDescriptor() should work.
		int s[2], my_pipe[2], fd;
		socketpair(AF_UNIX, SOCK_STREAM, 0, s);
		MessageChannel channel1(s[0]);
		MessageChannel channel2(s[1]);
		
		pipe(my_pipe);
		channel1.writeFileDescriptor(my_pipe[1]);
		fd = channel2.readFileDescriptor();
		
		char buf[5];
		write(fd, "hello", 5);
		close(fd);
		read(my_pipe[0], buf, 5);
		ensure(memcmp(buf, "hello", 5) == 0);
		
		close(s[0]);
		close(s[1]);
		close(my_pipe[0]);
		close(my_pipe[1]);
	}
	
	TEST_METHOD(8) {
		// write() should be able to construct a message that consists of only an empty string.
		// read() should be able to read a message that consists of only an empty string.
		vector<string> args;
		
		writer.write("", NULL);
		reader.read(args);
		ensure_equals(args.size(), 1u);
		ensure_equals(args[0], "");
	}
	
	TEST_METHOD(9) {
		// readScalar() should be able to read messages constructed by writeScalar().
		// This also tests readRaw()/writeRaw() because readScalar()/writeScalar() uses
		// them internally.
		writer.writeScalar("hello\n\r world!!!");
		writer.writeScalar("  and this is a second message");
		
		string output;
		ensure("End of stream has not been reached (1)", reader.readScalar(output));
		ensure_equals(output, "hello\n\r world!!!");
		
		ensure("End of stream has not been reached (2)", reader.readScalar(output));
		ensure_equals(output, "  and this is a second message");
	}
	
	TEST_METHOD(10) {
		// TODO:
		// writeScalar() should be able to produce messages that are compatible with the Ruby implementation.
		// readScalar() should be able to read messages produced by the Ruby implementation.
	}
	
	TEST_METHOD(11) {
		// TODO:
		// If we send a lot of different messages (including file descriptor passing),
		// and the other side sends the same stuff back to us, then MessageChannel
		// should be able to read them all, if done in the correct order.
	}
	
	// TODO: test end-of-file detection in readScalar()/readRaw()
}

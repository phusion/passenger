#include "TestSupport.h"
#include "MessageChannel.h"

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <oxt/thread.hpp>
#include <utility>

#include <cstring>
#include <cstdio>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace Passenger;
using namespace std;
using namespace boost;
using namespace oxt;

namespace tut {
	struct MessageChannelTest {
		MessageChannel reader, writer;
		int p[2];

		MessageChannelTest() {
			if (pipe(p) != 0) {
				throw SystemException("Cannot create a pipe", errno);
			}
			reader = MessageChannel(p[0]);
			writer = MessageChannel(p[1]);
		}
		
		~MessageChannelTest() {
			reader.close();
			writer.close();
		}
		
		static void writeDataAfterSomeTime(int fd, unsigned int sleepTimeInMsec) {
			try {
				syscalls::usleep(sleepTimeInMsec * 1000);
				syscalls::write(fd, "hi", 2);
			} catch (const boost::thread_interrupted &) {
				// Do nothing.
			}
		}

		static void writeDataSlowly(int fd, unsigned int bytesToWrite, unsigned int bytesPerSec) {
			try {
				for (unsigned i = 0; i < bytesToWrite && !boost::this_thread::interruption_requested(); i++) {
					syscalls::write(fd, "x", 1);
					syscalls::usleep(1000000 / bytesPerSec);
				}
			} catch (const boost::thread_interrupted &) {
				// Do nothing.
			}
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
			close(p[0]);
			close(p[1]);
			dup2(p1[0], 0);
			dup2(p2[1], 1);
			close(p1[0]);
			close(p1[1]);
			close(p2[0]);
			close(p2[1]);
			execlp("ruby", "ruby", "./stub/message_channel.rb", (char *) 0);
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
		boost::thread thr(bind(
			&MessageChannel::writeFileDescriptor,
			&channel1,
			my_pipe[1],
			true
		));
		fd = channel2.readFileDescriptor();
		thr.join();
		
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
		// This also tests readExact()/writeExact() because readScalar()/writeScalar() uses
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
		// writeScalar() should be able to produce messages that are compatible with the Ruby implementation.
		// readScalar() should be able to read messages produced by the Ruby implementation.
		int p1[2], p2[2];
		pid_t pid;
		
		pipe(p1);
		pipe(p2);
		pid = fork();
		if (pid == 0) {
			close(p[0]);
			close(p[1]);
			dup2(p1[0], 0);
			dup2(p2[1], 1);
			close(p1[0]);
			close(p1[1]);
			close(p2[0]);
			close(p2[1]);
			execlp("ruby", "ruby", "./stub/message_channel_2.rb", (void *) 0);
			perror("Cannot execute ruby");
			_exit(1);
		} else {
			MessageChannel reader(p2[0]);
			MessageChannel writer(p1[1]);
			string output;
			close(p1[0]);
			close(p2[1]);
			
			writer.writeScalar("hello world\n!\r!");
			ensure("End of file has not yet been reached (1)", reader.readScalar(output));
			ensure_equals(output, "hello world\n!\r!!!");
			
			writer.writeScalar("");
			ensure("End of file has not yet been reached (2)", reader.readScalar(output));
			ensure_equals(output, "??");
			writer.close();
			
			ensure("End of file has been reached", !reader.readScalar(output));
			reader.close();
			waitpid(pid, NULL, 0);
		}
	}
	
	TEST_METHOD(11) {
		// If we send a lot of different messages (including file descriptor passing),
		// and the other side sends the same stuff back to us, then MessageChannel
		// should be able to read them all, if done in the correct order.
		// writeScalar() should be able to produce messages that are compatible with the Ruby implementation.
		// readScalar() should be able to read messages produced by the Ruby implementation.
		int fd[2];
		pid_t pid;
		
		socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
		pid = fork();
		if (pid == 0) {
			close(p[0]);
			close(p[1]);
			dup2(fd[0], 3);
			close(fd[0]);
			close(fd[1]);
			execlp("ruby", "ruby", "./stub/message_channel_3.rb", (void *) 0);
			perror("Cannot execute ruby");
			_exit(1);
		} else {
			MessageChannel channel(fd[1]);
			close(fd[0]);
			
			vector<string> args;
			string output;
			int tmp[2];
			
			channel.write("hello ", "my!", "world", NULL);
			ensure("End of file has not yet been reached", channel.read(args));
			ensure_equals(args.size(), 3u);
			ensure_equals(args[0], "hello ");
			ensure_equals(args[1], "my!");
			ensure_equals(args[2], "world");
			
			channel.writeScalar("testing 123");
			ensure("End of file has not yet been reached", channel.readScalar(output));
			ensure_equals(output, "testing 123");
			
			pipe(tmp);
			close(tmp[0]);
			channel.writeFileDescriptor(tmp[1]);
			close(tmp[1]);
			int x = channel.readFileDescriptor();
			close(x);
			
			channel.write("the end", NULL);
			ensure("End of file has not yet been reached", channel.read(args));
			ensure_equals(args.size(), 1u);
			ensure_equals(args[0], "the end");
			
			ensure("End of file has been reached", !channel.read(args));
			channel.close();
			waitpid(pid, NULL, 0);
		}
	}
	
	TEST_METHOD(12) {
		// readScalar()/writeScalar() should be able to handle arbitrary binary data.
		string data;
		FILE *f = fopen("stub/garbage3.dat", "r");
		while (!feof(f)) {
			char buf[1024 * 32];
			size_t ret = fread(buf, 1, sizeof(buf), f);
			data.append(buf, ret);
		}
		fclose(f);
		
		pid_t pid = fork();
		if (pid == 0) {
			reader.close();
			writer.writeScalar(data);
			_exit(0);
		} else {
			writer.close();
			string result;
			reader.readScalar(result);
			ensure_equals(result, data);
			waitpid(pid, NULL, 0);
		}
	}
	
	TEST_METHOD(13) {
		// Test connected(), fileno() and close().
		int fd[2];
		pipe(fd);
		close(fd[1]);
		
		MessageChannel channel(fd[0]);
		ensure(channel.connected());
		ensure_equals(channel.filenum(), fd[0]);
		
		channel.close();
		ensure_equals(channel.filenum(), -1);
		ensure(!channel.connected());
	}
	
	TEST_METHOD(14) {
		// close() sets the file descriptor to -1 even if closing failed.
		int fd[2];
		pipe(fd);
		close(fd[0]);
		close(fd[1]);
		
		MessageChannel channel(fd[0]);
		bool gotException;
		try {
			channel.close();
			gotException = false;
		} catch (...) {
			gotException = true;
		}
		if (!gotException) {
			fail("close() should have failed");
		}
		ensure_equals(channel.filenum(), -1);
		ensure(!channel.connected());
	}
	
	TEST_METHOD(25) {
		// readScalar() doesn't throw SecurityException if maxSize is
		// given but the available amount of data equals maxSize.
		string str;
		writer.writeScalar("hello");
		reader.readScalar(str, 5);
	}
	
	TEST_METHOD(26) {
		// readScalar() throws SecurityException if there's too much data to read.
		string str;
		
		writer.writeScalar("hello");
		try {
			reader.readScalar(str, 4);
			fail("SecurityException expected");
		} catch (const SecurityException &) {
			// Pass.
		}
	}
	
	TEST_METHOD(27) {
		// readScalar() throws TimeoutException if no data was received within the timeout.
		unsigned long long timeout = 30;
		string str;
		try {
			reader.readScalar(str, 0, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5);
		}
	}
	
	TEST_METHOD(28) {
		// readScalar() throws TimeoutException if not enough header data was received
		// within the timeout.
		unsigned long long timeout = 30;
		string str;
		writeExact(writer.filenum(), "xxx", 3); // A part of a random 32-bit integer header.
		try {
			reader.readScalar(str, 0, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5);
		}
	}
	
	TEST_METHOD(29) {
		// readScalar() throws TimeoutException if the header data was received but no
		// body data was received within the timeout.
		unsigned long long timeout = 30;
		string str;
		writer.writeUint32(1024); // Dummy header.
		try {
			reader.readScalar(str, 0, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("The passed time is deducted from timeout", timeout < 5);
		}
	}
	
	TEST_METHOD(30) {
		// readScalar() throws TimeoutException if the header data was received but not
		// enough body data was received within the timeout.
		string str;
		writer.writeUint32(1024); // Dummy header.
		
		// Write a dummy body at 100 bytes per sec, or 1 byte every 10 msec.
		// Takes 10 seconds.
		TempThread thr(boost::bind(&writeDataSlowly, writer.filenum(), 1000, 100));
		
		unsigned long long timeout = 35;
		Timer timer;
		try {
			reader.readScalar(str, 0, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			unsigned long long elapsed = timer.elapsed();
			ensure("Spent at least 35 msec waiting", elapsed >= 35);
			ensure("Spent at most 60 msec waiting", elapsed <= 60);
			ensure("The passed time is deducted from timeout", timeout < 5);
		}
	}
	
	TEST_METHOD(31) {
		// readScalar() returns if enough data was received within the specified timeout.
		string str;
		unsigned long long timeout = 1000;
		
		writer.writeUint32(250);
		TempThread thr(boost::bind(&writeDataSlowly, writer.filenum(), 250, 1000));
		
		reader.readScalar(str, 0, &timeout);
		ensure("Spent at least 250 msec waiting", timeout <= 1000 - 250);
		ensure("Spent at most 500 msec waiting", timeout >= 1000 - 500);
	}
	
	TEST_METHOD(32) {
		// Test readUint32() and writeUint32().
		writer.writeUint32(0);
		writer.writeUint32(1);
		writer.writeUint32(1024);
		writer.writeUint32(3000000000u);
		
		unsigned int i;
		ensure(reader.readUint32(i));
		ensure_equals(i, 0u);
		ensure(reader.readUint32(i));
		ensure_equals(i, 1u);
		ensure(reader.readUint32(i));
		ensure_equals(i, 1024u);
		ensure(reader.readUint32(i));
		ensure_equals(i, 3000000000u);
	}
	
	TEST_METHOD(33) {
		// readUint32() returns false if EOF was reached prematurely.
		writeExact(writer.filenum(), "x", 1);
		writer.close();
		unsigned int i;
		ensure(!reader.readUint32(i));
	}
	
	TEST_METHOD(34) {
		// readUint32() throws TimeoutException if no data was available within the timeout.
		unsigned long long timeout = 30;
		unsigned int i;
		try {
			reader.readUint32(i, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure("The spent time is deducted from timeout", timeout < 5);
		}
	}
	
	TEST_METHOD(35) {
		// readUint32() throws TimeoutException if not enough data was available within the timeout.
		unsigned long long timeout = 30;
		unsigned int i;
		writeExact(writer.filenum(), "xx", 2);
		try {
			reader.readUint32(i, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &e) {
			ensure("The spent time is deducted from timeout", timeout < 5);
		}
	}
	
	TEST_METHOD(36) {
		// readUint32() throws TimeoutException if timeout is 0 and no data
		// is immediately available.
		unsigned long long timeout = 0;
		unsigned int i;
		try {
			reader.readUint32(i, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &) {
			ensure_equals("Timeout is unchanged", timeout, 0u);
		}
	}
	
	TEST_METHOD(37) {
		// readUint32() throws TimeoutException if timeout is 0 and not enough
		// data is immediately available.
		unsigned long long timeout = 0;
		unsigned int i;
		writeExact(writer.filenum(), "xx", 2);
		try {
			reader.readUint32(i, &timeout);
			fail("TimeoutException expected");
		} catch (const TimeoutException &e) {
			ensure_equals("Timeout unchanged", timeout, 0u);
		}
	}
}

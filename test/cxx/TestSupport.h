#ifndef _TEST_SUPPORT_H_
#define _TEST_SUPPORT_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <string>
#include <vector>
#include <exception>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <utime.h>

#include <oxt/thread.hpp>
#include <oxt/tracable_exception.hpp>

#include "../tut/tut.h"
#include <ResourceLocator.h>
#include <ServerInstanceDir.h>
#include <BackgroundEventLoop.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/SystemTime.h>
#include <Utils/json-forwards.h>

extern "C" {
	struct ev_loop;
	struct ev_async;
}

namespace Passenger {
	class SafeLibev;
}

namespace TestSupport {

using namespace std;
using namespace Passenger;
using namespace oxt;


#define SHOW_EXCEPTION_BACKTRACE(code)                                \
	do {                                                          \
		try {                                                 \
			code                                          \
		} catch (const tracable_exception &e) {               \
			cerr << e.what() << "\n" << e.backtrace();    \
			throw;                                        \
		}                                                     \
	} while (0)

#define EVENTUALLY2(deadlineMsec, sleepTimeMsec, code)					\
	do {										\
		unsigned long long deadlineTime = SystemTime::getMsec(true) + deadlineMsec;	\
		bool result = false;							\
		while (!result && SystemTime::getMsec(true) < deadlineTime) {		\
			{										\
				code								\
			}										\
			if (!result) {							\
				usleep(sleepTimeMsec * 1000);				\
			}								\
		}									\
		if (!result) {								\
			fail("EVENTUALLY(" #code ") failed");				\
		}									\
	} while (0)

#define EVENTUALLY(deadlineSec, code) EVENTUALLY2(deadlineSec * 1000, 10, code)

#define SHOULD_NEVER_HAPPEN(deadline, code)						\
	do {										\
		unsigned long long deadlineTime = SystemTime::getMsec(true) + deadline;	\
		bool result = false;							\
		while (!result && SystemTime::getMsec(true) < deadlineTime) {		\
			code								\
			if (!result) {							\
				usleep(10000);						\
			}								\
		}									\
		if (result) {								\
			fail("SHOULD_NEVER_HAPPEN(" #code ") failed");			\
		}									\
	} while (0)

// Do not run some tests in the Vagrant development environment because
// they don't work over NFS.
#define DONT_RUN_IN_VAGRANT() \
	do { \
		if (getenv("PASSENGER_VAGRANT_ENVIRONMENT") != NULL) { \
			return; \
		} \
	} while (false)


extern ResourceLocator *resourceLocator;
extern Json::Value testConfig;


/**
 * Create a server instance directory and generation with default parameters,
 * suitable for unit testing.
 */
void createServerInstanceDirAndGeneration(ServerInstanceDirPtr &serverInstanceDir,
                                          ServerInstanceDir::GenerationPtr &generation);

/**
 * Writes zeroes into the given file descriptor its buffer is full (i.e.
 * the next write will block).
 *
 * @throws SystemException
 */
void writeUntilFull(int fd);

/**
 * Look for 'toFind' inside the given file, replace it with 'replaceWith' and write
 * the result back to the file. Only the first occurence of 'toFind' is replaced.
 *
 * @throws FileSystemException
 */
void replaceStringInFile(const char *filename, const string &toFind, const string &replaceWith);

/**
 * Returns whether 'str' contains the given substring.
 */
bool containsSubstring(const StaticString &str, const StaticString &substr);

/**
 * Writes the given data into the given file.
 *
 * @throws FileSystemException
 */
void writeFile(const string &filename, const string &contents);

/**
 * Touch the given file: create the file if it doesn't exist, update its
 * timestamp if it does. If the <tt>timestamp</tt> argument is -1, then
 * the current system time will be used, otherwise the given timestamp
 * will be used.
 *
 * @throws FileSystemException
 */
void touchFile(const char *filename, time_t timestamp = (time_t) - 1);

/**
 * Returns all filenames in the given directory.
 */
vector<string> listDir(const string &path);

/**
 * Returns the name of the primary group of the given user.
 */
string getPrimaryGroupName(const string &username);


/**
 * Class which creates a temporary directory of the given name, and deletes
 * it upon destruction.
 */
class TempDir {
private:
	string name;
	bool ignoreRemoveErrors;
public:
	TempDir(const string &name, bool _ignoreRemoveErrors = false) {
		this->name = name;
		if (mkdir(name.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0 && errno != EEXIST) {
			int e = errno;
			string message = "Cannot create directory '";
			message.append(name);
			message.append("'");
			throw FileSystemException(message, e, name);
		}
		ignoreRemoveErrors = _ignoreRemoveErrors;
	}
	
	~TempDir() {
		if (ignoreRemoveErrors) {
			try {
				removeDirTree(name);
			} catch (const RuntimeException &) {
				// Do nothing.
			}
		} else {
			removeDirTree(name);
		}
	}
};


/**
 * Creates a temporary copy of the given directory. This copy is deleted
 * upon object destruction.
 */
class TempDirCopy {
private:
	string dir;
public:
	TempDirCopy(const string &source, const string &dest) {
		dir = dest;
		removeDirTree(dest);
		
		char command[1024];
		snprintf(command, sizeof(command), "cp -pR \"%s\" \"%s\"",
			source.c_str(), dest.c_str());
		pid_t pid = fork();
		if (pid == 0) {
			resetSignalHandlersAndMask();
			disableMallocDebugging();
			closeAllFileDescriptors(2);
			execlp("/bin/sh", "/bin/sh", "-c", command, (char * const) 0);
			_exit(1);
		} else if (pid == -1) {
			int e = errno;
			throw SystemException("Cannot fork()", e);
		} else {
			waitpid(pid, NULL, 0);
		}
	}
	
	~TempDirCopy() {
		removeDirTree(dir);
	}
};


/**
 * Class which deletes the given file upon destruction.
 */
class DeleteFileEventually {
private:
	string filename;
public:
	DeleteFileEventually(const string &filename, bool deleteNow = true) {
		this->filename = filename;
		if (deleteNow) {
			unlink(filename.c_str());
		}
	}
	
	~DeleteFileEventually() {
		unlink(filename.c_str());
	}
};


/**
 * Spawns a thread which will be interrupted and joined when this TempThread
 * object is destroyed.
 */
class TempThread {
public:
	oxt::thread thread;
	
	TempThread(boost::function<void ()> func)
		: thread(boost::bind(runAndPrintExceptions, func, true))
		{ }
	
	~TempThread() {
		thread.interrupt_and_join();
	}
};


class AtomicInt {
private:
	mutable boost::mutex lock;
	int val;
public:
	AtomicInt() {
		val = 0;
	}
	
	AtomicInt(int value) {
		val = value;
	}
	
	AtomicInt(const AtomicInt &other) {
		val = other.val;
	}
	
	int get() const {
		boost::lock_guard<boost::mutex> l(lock);
		return val;
	}
	
	void set(int value) {
		boost::lock_guard<boost::mutex> l(lock);
		val = value;
	}
	
	AtomicInt &operator=(int value) {
		set(value);
		return *this;
	}
	
	AtomicInt &operator++() {
		boost::lock_guard<boost::mutex> l(lock);
		val++;
		return *this;
	}
	
	AtomicInt operator++(int) {
		boost::lock_guard<boost::mutex> l(lock);
		AtomicInt temp(*this);
		val++;
		return temp;
	}
	
	operator int() const {
		return get();
	}
};


} // namespace TestSupport

using namespace TestSupport;

#endif /* _TEST_SUPPORT_H_ */

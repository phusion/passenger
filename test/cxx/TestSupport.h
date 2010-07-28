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
#include "ServerInstanceDir.h"
#include "Exceptions.h"
#include "Utils.h"
#include "Utils/SystemTime.h"

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

#define EVENTUALLY(deadline, code)					\
	do {								\
		time_t deadlineTime = time(NULL) + deadline;		\
		bool result = false;					\
		while (!result && time(NULL) < deadlineTime) {		\
			code						\
			if (!result) {					\
				usleep(10000);				\
			}						\
		}							\
		if (!result) {						\
			fail("EVENTUALLY(" #code ") failed");		\
		}							\
	} while (0)

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


/**
 * Create a server instance directory and generation with default parameters,
 * suitable for unit testing.
 */
void createServerInstanceDirAndGeneration(ServerInstanceDirPtr &serverInstanceDir,
                                          ServerInstanceDir::GenerationPtr &generation);

/**
 * Read all data from the given file until EOF.
 *
 * @throws SystemException
 */
string readAll(const string &filename);

/**
 * Read all data from the given file descriptor until EOF.
 *
 * @throws SystemException
 */
string readAll(int fd);

/**
 * Look for 'toFind' inside 'str', replace it with 'replaceWith' and return the result.
 * Only the first occurence of 'toFind' is replaced.
 */
string replaceString(const string &str, const string &toFind, const string &replaceWith);

/**
 * Look for 'toFind' inside the given file, replace it with 'replaceWith' and write
 * the result back to the file. Only the first occurence of 'toFind' is replaced.
 *
 * @throws FileSystemException
 */
void replaceStringInFile(const char *filename, const string &toFind, const string &replaceWith);

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
public:
	TempDir(const string &name) {
		this->name = name;
		if (mkdir(name.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
			int e = errno;
			string message = "Cannot create directory '";
			message.append(name);
			message.append("'");
			throw FileSystemException(message, e, name);
		}
	}
	
	~TempDir() {
		removeDirTree(name);
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
		system(command);
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
	DeleteFileEventually(const string &filename) {
		this->filename = filename;
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
		: thread(func)
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
	
	int get() const {
		lock_guard<boost::mutex> l(lock);
		return val;
	}
	
	void set(int value) {
		lock_guard<boost::mutex> l(lock);
		val = value;
	}
	
	AtomicInt &operator=(int value) {
		set(value);
		return *this;
	}
	
	operator int() const {
		return get();
	}
};

} // namespace TestSupport

using namespace TestSupport;

#endif /* _TEST_SUPPORT_H_ */

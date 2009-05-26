#ifndef _TEST_SUPPORT_H_
#define _TEST_SUPPORT_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <string>
#include <exception>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <utime.h>

#include "Exceptions.h"
#include "Utils.h"

namespace Test {

using namespace std;
using namespace Passenger;

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
 * Touch the given file: create the file if it doesn't exist, update its
 * timestamp if it does. If the <tt>timestamp</tt> argument is -1, then
 * the current system time will be used, otherwise the given timestamp
 * will be used.
 *
 * @throws FileSystemException
 */
void touchFile(const char *filename, time_t timestamp = (time_t) - 1);

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

} // namespace Test

#endif /* _TEST_SUPPORT_H_ */

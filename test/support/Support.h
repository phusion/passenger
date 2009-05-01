#ifndef _TEST_SUPPORT_H_
#define _TEST_SUPPORT_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <string>
#include <exception>
#include <cerrno>
#include <cstring>

namespace Test {

using namespace std;

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
			cerr << "Cannot create directory '" << name << "': " <<
				strerror(e) <<" (" << e << ")" << endl;
			throw exception();
		}
	}
	
	~TempDir() {
		string command("rm -rf \"");
		command.append(name);
		command.append("\"");
		system(command.c_str());
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
	
	DeleteFileEventually() {
		unlink(filename.c_str());
	}
};

} // namespace Test

#endif /* _TEST_SUPPORT_H_ */

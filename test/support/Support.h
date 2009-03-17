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

} // namespace Test

#endif /* _TEST_SUPPORT_H_ */

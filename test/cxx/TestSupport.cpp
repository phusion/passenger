#include <TestSupport.h>
#include "../support/valgrind.h"
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <cassert>
#include <FileTools/FileManip.h>
#include <SystemTools/UserDatabase.h>
#include <Utils/ScopeGuard.h>
#include <jsoncpp/json.h>

namespace TestSupport {

LoggingKit::Level defaultLogLevel = (LoggingKit::Level) DEFAULT_LOG_LEVEL;
ResourceLocator *resourceLocator = NULL;
Json::Value testConfig;


void
createInstanceDir(InstanceDirectoryPtr &instanceDir) {
	InstanceDirectory::CreationOptions options;
	OsUser osUser;

	if (!lookupSystemUserByName("nobody", osUser)) {
		throw RuntimeException("OS user account 'nobody' does not exist");
	}

	options.prefix = "passenger-test";
	options.userSwitching = geteuid() == 0;
	options.defaultUid = osUser.pwd.pw_uid;
	options.defaultGid = osUser.pwd.pw_gid;
	instanceDir = boost::make_shared<InstanceDirectory>(options);
}

void
writeUntilFull(int fd) {
	int flags, ret;
	char buf[1024];

	memset(buf, 0, sizeof(buf));
	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	while (true) {
		ret = write(fd, buf, sizeof(buf));
		if (ret == -1) {
			int e = errno;
			if (e == EAGAIN) {
				break;
			} else {
				throw SystemException("write() failed", e);
			}
		}
	}
	while (true) {
		ret = write(fd, buf, 50);
		if (ret == -1) {
			int e = errno;
			if (e == EAGAIN) {
				break;
			} else {
				throw SystemException("write() failed", e);
			}
		}
	}
	while (true) {
		ret = write(fd, buf, 1);
		if (ret == -1) {
			int e = errno;
			if (e == EAGAIN) {
				break;
			} else {
				throw SystemException("write() failed", e);
			}
		}
	}

	fcntl(fd, F_SETFL, flags);
}

bool
containsSubstring(const StaticString &str, const StaticString &substr) {
	return str.find(substr) != string::npos;
}

void
writeFile(const string &filename, const string &contents) {
	createFile(filename, contents);
}

void
touchFile(const char *filename, time_t timestamp) {
	FILE *f = fopen(filename, "a");
	if (f != NULL) {
		fclose(f);
	} else if (errno != EISDIR) {
		int e = errno;
		string message = "Cannot touch file '";
		message.append(filename);
		message.append("'");
		throw FileSystemException(message, e, filename);
	}

	if (timestamp != (time_t) -1) {
		struct utimbuf times;
		times.actime = timestamp;
		times.modtime = timestamp;
		utime(filename, &times);
	}
}

string
getPrimaryGroupName(const string &username) {
	OsUser osUser;

	if (lookupSystemUserByName(username, osUser)) {
		OsGroup osGroup;
		if (lookupSystemGroupByGid(osUser.pwd.pw_gid, osGroup)) {
			return osGroup.grp.gr_name;
		} else {
			throw RuntimeException("OS group account with GID "
				+ toString(osUser.pwd.pw_gid) + " does not exist");
		}
	} else {
		throw RuntimeException("OS user account " + username + " does not exist");
	}
}


// Shared test setup code.
TestBase::TestBase() {
	if (LoggingKit::getLevel() != defaultLogLevel) {
		LoggingKit::setLevel(defaultLogLevel);
	}
}


} // namespace TestSupport

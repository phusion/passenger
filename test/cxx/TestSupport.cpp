#include "TestSupport.h"
#include "../support/valgrind.h"
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <cassert>
#include <eio.h>
#include <BackgroundEventLoop.cpp>
#include <Utils/IOUtils.h>
#include <Utils/ScopeGuard.h>
#include <Utils/json.h>

namespace TestSupport {

ResourceLocator *resourceLocator = NULL;
Json::Value testConfig;


void
createInstanceDir(InstanceDirectoryPtr &instanceDir) {
	InstanceDirectory::CreationOptions options;
	struct passwd *pwUser;

	pwUser = getpwnam("nobody");
	options.prefix = "passenger-test";
	options.userSwitching = geteuid() == 0;
	options.defaultUid = pwUser->pw_uid;
	options.defaultGid = pwUser->pw_gid;
	instanceDir = boost::make_shared<InstanceDirectory>(options);
}

static int
doNothing(eio_req *req) {
	return 0;
}

void
initializeLibeio() {
	eio_set_idle_timeout(1);
	eio_set_min_parallel(0);
	eio_set_max_parallel(1);
	eio_set_max_idle(0);
	if (RUNNING_ON_VALGRIND) {
		// Start an EIO thread to warm up Valgrind.
		eio_nop(0, doNothing, NULL);
	}
}

void
shutdownLibeio() {
	// For some reason, eio_nreqs() and eio_npending() never reach 0.
	// We're probably not shutting down libeio correctly.
	// As a workaround, we wait for 20 ms after deinitializing.
	while (eio_nready() > 0) {
		usleep(10000);
	}
	eio_deinit();
	usleep(20000);
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

void
replaceStringInFile(const char *filename, const string &toFind, const string &replaceWith) {
	string content = readAll(filename);
	FILE *f = fopen(filename, "w");
	if (f == NULL) {
		int e = errno;
		string message = "Cannot open file '";
		message.append(filename);
		message.append("' for writing");
		throw FileSystemException(message, e, filename);
	} else {
		StdioGuard guard(f);
		content = replaceString(content, toFind, replaceWith);
		fwrite(content.data(), 1, content.size(), f);
	}
}

bool
containsSubstring(const StaticString &str, const StaticString &substr) {
	return str.find(substr) != string::npos;
}

void
writeFile(const string &filename, const string &contents) {
	FILE *f = fopen(filename.c_str(), "w");
	if (f == NULL) {
		int e = errno;
		string message = "Cannot open file '";
		message.append(filename);
		message.append("' for writing");
		throw FileSystemException(message, e, filename);
	} else {
		StdioGuard guard(f);
		fwrite(contents.data(), 1, contents.size(), f);
	}
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

vector<string>
listDir(const string &path) {
	vector<string> result;
	DIR *d = opendir(path.c_str());
	struct dirent *ent;

	if (d == NULL) {
		int e = errno;
		throw FileSystemException("Cannot open directory " + path,
			e, path);
	}
	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}
		result.push_back(ent->d_name);
	}
	return result;
}

string
getPrimaryGroupName(const string &username) {
	struct passwd *user;
	struct group  *group;

	user = getpwnam(username.c_str());
	if (user == NULL) {
		throw RuntimeException(string("User '") + username + "' does not exist.");
	}
	group = getgrgid(user->pw_gid);
	if (group == NULL) {
		throw RuntimeException(string("Primary group for user '") + username + "' does not exist.");
	}

	return group->gr_name;
}


} // namespace TestSupport

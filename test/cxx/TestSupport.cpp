#include "TestSupport.h"
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <cassert>
#include <BackgroundEventLoop.cpp>
#include <Utils/IOUtils.h>
#include <Utils/ScopeGuard.h>
#include <Utils/json.h>

namespace TestSupport {

ResourceLocator *resourceLocator = NULL;
Json::Value testConfig;


void createServerInstanceDirAndGeneration(ServerInstanceDirPtr &serverInstanceDir,
                                          ServerInstanceDir::GenerationPtr &generation)
{
	string path = "/tmp/passenger-test." + toString(getpid());
	serverInstanceDir.reset(new ServerInstanceDir(path));
	generation = serverInstanceDir->newGeneration(geteuid() == 0,
		"nobody", getPrimaryGroupName("nobody"),
		geteuid(), getegid());
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

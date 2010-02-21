#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "TestSupport.h"

namespace TestSupport {

void createServerInstanceDirAndGeneration(ServerInstanceDirPtr &serverInstanceDir,
                                          ServerInstanceDir::GenerationPtr &generation)
{
	serverInstanceDir.reset(new ServerInstanceDir(getpid()));
	generation = serverInstanceDir->newGeneration(geteuid() == 0,
		"nobody", getPrimaryGroupName("nobody"),
		geteuid(), getegid());
}

string
readAll(const string &filename) {
	FILE *f = fopen(filename.c_str(), "rb");
	if (f != NULL) {
		try {
			string result = readAll(fileno(f));
			fclose(f);
			return result;
		} catch (...) {
			fclose(f);
			throw;
		}
	} else {
		int e = errno;
		throw FileSystemException("Cannot open '" + filename + "' for reading",
			e, filename);
	}
}

string
readAll(int fd) {
	string result;
	char buf[1024 * 32];
	ssize_t ret;
	while (true) {
		do {
			ret = read(fd, buf, sizeof(buf));
		} while (ret == -1 && errno == EINTR);
		if (ret == 0) {
			break;
		} else if (ret == -1) {
			throw SystemException("Cannot read from socket", errno);
		} else {
			result.append(buf, ret);
		}
	}
	return result;
}

string
replaceString(const string &str, const string &toFind, const string &replaceWith) {
	string::size_type pos = str.find(toFind);
	if (pos == string::npos) {
		return str;
	} else {
		string result(str);
		return result.replace(pos, toFind.size(), replaceWith);
	}
}

void
replaceStringInFile(const char *filename, const string &toFind, const string &replaceWith) {
	FILE *f = fopen(filename, "r");
	if (f == NULL) {
		int e = errno;
		string message = "Cannot open file '";
		message.append(filename);
		message.append("' for reading");
		throw FileSystemException(message, e, filename);
	}
	string content(readAll(fileno(f)));
	fclose(f);
	
	f = fopen(filename, "w");
	if (f == NULL) {
		int e = errno;
		string message = "Cannot open file '";
		message.append(filename);
		message.append("' for writing");
		throw FileSystemException(message, e, filename);
	}
	content = replaceString(content, toFind, replaceWith);
	fwrite(content.data(), 1, content.size(), f);
	fclose(f);
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
	}
	fwrite(contents.data(), 1, contents.size(), f);
	fclose(f);
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

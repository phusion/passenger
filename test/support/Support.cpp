#include <unistd.h>
#include "Support.h"

namespace Test {

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

} // namespace Test

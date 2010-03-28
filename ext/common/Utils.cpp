/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

#include <oxt/system_calls.hpp>
#include <boost/thread.hpp>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <cassert>
#include <cstring>
#include <netdb.h>
#include <libgen.h>
#include <fcntl.h>
#include <limits.h>
#include "FileDescriptor.h"
#include "MessageChannel.h"
#include "MessageServer.h"
#include "ResourceLocator.h"
#include "Exceptions.h"
#include "Utils.h"
#include "Utils/Base64.h"
#include "Utils/CachedFileStat.hpp"

#define SPAWN_SERVER_SCRIPT_NAME "passenger-spawn-server"

#ifndef HOST_NAME_MAX
	#if defined(_POSIX_HOST_NAME_MAX)
		#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
	#elif defined(_SC_HOST_NAME_MAX)
		#define HOST_NAME_MAX sysconf(_SC_HOST_NAME_MAX)
	#else
		#define HOST_NAME_MAX 255
	#endif
#endif

namespace Passenger {

static string passengerTempDir;

namespace {
	/**
	 * Given a filename, FileGuard will unlink the file in its destructor, unless
	 * commit() was called. Used in file operation functions that don't want to
	 * leave behind half-finished files after error conditions.
	 */
	struct FileGuard {
		string filename;
		bool committed;
		
		FileGuard(const string &filename) {
			this->filename = filename;
			committed = false;
		}
		
		~FileGuard() {
			if (!committed) {
				int ret;
				do {
					ret = unlink(filename.c_str());
				} while (ret == -1 && errno == EINTR);
			}
		}
		
		void commit() {
			committed = true;
		}
	};
}


int
atoi(const string &s) {
	return ::atoi(s.c_str());
}

long
atol(const string &s) {
	return ::atol(s.c_str());
}

void
split(const string &str, char sep, vector<string> &output) {
	string::size_type start, pos;
	start = 0;
	output.clear();
	while ((pos = str.find(sep, start)) != string::npos) {
		output.push_back(str.substr(start, pos - start));
		start = pos + 1;
	}
	output.push_back(str.substr(start));
}

bool
fileExists(const StaticString &filename, CachedFileStat *cstat, unsigned int throttleRate) {
	return getFileType(filename, cstat, throttleRate) == FT_REGULAR;
}

FileType
getFileType(const StaticString &filename, CachedFileStat *cstat, unsigned int throttleRate) {
	struct stat buf;
	int ret;
	
	if (cstat != NULL) {
		ret = cstat->stat(filename.toString(), &buf, throttleRate);
	} else {
		ret = stat(filename.c_str(), &buf);
	}
	if (ret == 0) {
		if (S_ISREG(buf.st_mode)) {
			return FT_REGULAR;
		} else if (S_ISDIR(buf.st_mode)) {
			return FT_DIRECTORY;
		} else {
			return FT_OTHER;
		}
	} else {
		if (errno == ENOENT) {
			return FT_NONEXISTANT;
		} else {
			int e = errno;
			string message("Cannot stat '");
			message.append(filename);
			message.append("'");
			throw FileSystemException(message, e, filename);
		}
	}
}

void
createFile(const string &filename, const StaticString &contents, mode_t permissions, uid_t owner,
	gid_t group, bool overwrite)
{
	FileDescriptor fd;
	int ret, e, options;
	
	options = O_WRONLY | O_CREAT | O_TRUNC;
	if (!overwrite) {
		options |= O_EXCL;
	}
	do {
		fd = open(filename.c_str(), options, permissions);
	} while (fd == -1 && errno == EINTR);
	if (fd != -1) {
		FileGuard guard(filename);
		
		// The file permission may not be as expected because of the active
		// umask, so fchmod() it here to ensure correct permissions.
		do {
			ret = fchmod(fd, permissions);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			e = errno;
			throw FileSystemException("Cannot set permissions on " + filename,
				e, filename);
		}
		
		if (owner != USER_NOT_GIVEN && group != GROUP_NOT_GIVEN) {
			if (owner == USER_NOT_GIVEN) {
				owner = (uid_t) -1; // Don't let fchown change file owner.
			}
			if (group == GROUP_NOT_GIVEN) {
				group = (gid_t) -1; // Don't let fchown change file group.
			}
			do {
				ret = fchown(fd, owner, group);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				e = errno;
				throw FileSystemException("Cannot set ownership for " + filename,
					e, filename);
			}
		}
		
		try {
			MessageChannel(fd).writeRaw(contents);
			fd.close();
		} catch (const SystemException &e) {
			throw FileSystemException("Cannot write to file " + filename,
				e.code(), filename);
		}
		guard.commit();
	} else {
		e = errno;
		if (overwrite || e != EEXIST) {
			throw FileSystemException("Cannot create file " + filename,
				e, filename);
		}
	}
}

string
canonicalizePath(const string &path) {
	#ifdef __GLIBC__
		// We're using a GNU extension here. See the 'BUGS'
		// section of the realpath(3) Linux manpage for
		// rationale.
		char *tmp = realpath(path.c_str(), NULL);
		if (tmp == NULL) {
			int e = errno;
			string message;
			
			message = "Cannot resolve the path '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		} else {
			string result(tmp);
			free(tmp);
			return result;
		}
	#else
		char tmp[PATH_MAX];
		if (realpath(path.c_str(), tmp) == NULL) {
			int e = errno;
			string message;
			
			message = "Cannot resolve the path '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		} else {
			return tmp;
		}
	#endif
}

string
resolveSymlink(const string &path) {
	char buf[PATH_MAX];
	ssize_t size;
	
	size = readlink(path.c_str(), buf, sizeof(buf) - 1);
	if (size == -1) {
		if (errno == EINVAL) {
			return path;
		} else {
			int e = errno;
			string message = "Cannot resolve possible symlink '";
			message.append(path);
			message.append("'");
			throw FileSystemException(message, e, path);
		}
	} else {
		buf[size] = '\0';
		if (buf[0] == '\0') {
			string message = "The file '";
			message.append(path);
			message.append("' is a symlink, and it refers to an empty filename. This is not allowed.");
			throw FileSystemException(message, ENOENT, path);
		} else if (buf[0] == '/') {
			// Symlink points to an absolute path.
			return buf;
		} else {
			return extractDirName(path) + "/" + buf;
		}
	}
}

string
extractDirName(const StaticString &path) {
	char *path_copy = strdup(path.c_str());
	char *result = dirname(path_copy);
	string result_string(result);
	free(path_copy);
	return result_string;
}

string
extractBaseName(const StaticString &path) {
	char *path_copy = strdup(path.c_str());
	string result_string = basename(path_copy);
	free(path_copy);
	return result_string;
}

string
escapeForXml(const string &input) {
	string result(input);
	string::size_type input_pos = 0;
	string::size_type input_end_pos = input.size();
	string::size_type result_pos = 0;
	
	while (input_pos < input_end_pos) {
		const unsigned char ch = input[input_pos];
		
		if ((ch >= 'A' && ch <= 'z')
		 || (ch >= '0' && ch <= '9')
		 || ch == '/' || ch == ' ' || ch == '_' || ch == '.'
		 || ch == ':' || ch == '+' || ch == '-') {
			// This is an ASCII character. Ignore it and
			// go to next character.
			result_pos++;
		} else {
			// Not an ASCII character; escape it.
			char escapedCharacter[sizeof("&#255;") + 1];
			int size;
			
			size = snprintf(escapedCharacter,
				sizeof(escapedCharacter) - 1,
				"&#%d;",
				(int) ch);
			if (size < 0) {
				throw std::bad_alloc();
			}
			escapedCharacter[sizeof(escapedCharacter) - 1] = '\0';
			
			result.replace(result_pos, 1, escapedCharacter, size);
			result_pos += size;
		}
		input_pos++;
	}
	
	return result;
}

void
setNonBlocking(int fd) {
	int flags, ret;
	
	do {
		flags = fcntl(fd, F_GETFL);
	} while (flags == -1 && errno == EINTR);
	if (flags == -1) {
		int e = errno;
		throw SystemException("Cannot set socket to non-blocking mode: "
			"cannot get socket flags",
			e);
	}
	do {
		ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		int e = errno;
		throw SystemException("Cannot set socket to non-blocking mode: "
			"cannot set socket flags",
			e);
	}
}

string
getProcessUsername() {
	struct passwd pwd, *result;
	char strings[1024];
	int ret;
	
	result = (struct passwd *) NULL;
	do {
		ret = getpwuid_r(getuid(), &pwd, strings, sizeof(strings), &result);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		result = (struct passwd *) NULL;
	}
	
	if (result == (struct passwd *) NULL) {
		snprintf(strings, sizeof(strings), "UID %lld", (long long) getuid());
		strings[sizeof(strings) - 1] = '\0';
		return strings;
	} else {
		return result->pw_name;
	}
}

mode_t
parseModeString(const StaticString &mode) {
	mode_t modeBits = 0;
	vector<string> clauses;
	vector<string>::iterator it;
	
	split(mode, ',', clauses);
	for (it = clauses.begin(); it != clauses.end(); it++) {
		const string &clause = *it;
		
		if (clause.empty()) {
			continue;
		} else if (clause.size() < 2 || clause[1] != '=') {
			throw InvalidModeStringException("Invalid mode clause specification '" + clause + "'");
		}
		
		switch (clause[0]) {
		case 'u':
			for (string::size_type i = 2; i < clause.size(); i++) {
				switch (clause[i]) {
				case 'r':
					modeBits |= S_IRUSR;
					break;
				case 'w':
					modeBits |= S_IWUSR;
					break;
				case 'x':
					modeBits |= S_IXUSR;
					break;
				case 's':
					modeBits |= S_ISUID;
					break;
				default:
					throw InvalidModeStringException("Invalid permission '" +
						string(1, clause[i]) +
						"' in mode clause specification '" +
						clause + "'");
				}
			}
			break;
		case 'g':
			for (string::size_type i = 2; i < clause.size(); i++) {
				switch (clause[i]) {
				case 'r':
					modeBits |= S_IRGRP;
					break;
				case 'w':
					modeBits |= S_IWGRP;
					break;
				case 'x':
					modeBits |= S_IXGRP;
					break;
				case 's':
					modeBits |= S_ISGID;
					break;
				default:
					throw InvalidModeStringException("Invalid permission '" +
						string(1, clause[i]) +
						"' in mode clause specification '" +
						clause + "'");
				}
			}
			break;
		case 'o':
			for (string::size_type i = 2; i < clause.size(); i++) {
				switch (clause[i]) {
				case 'r':
					modeBits |= S_IROTH;
					break;
				case 'w':
					modeBits |= S_IWOTH;
					break;
				case 'x':
					modeBits |= S_IXOTH;
					break;
				default:
					throw InvalidModeStringException("Invalid permission '" +
						string(1, clause[i]) +
						"' in mode clause specification '" +
						clause + "'");
				}
			}
			break;
		default:
			throw InvalidModeStringException("Invalid owner '" + string(1, clause[0]) +
				"' in mode clause specification '" + clause + "'");
		}
	}
	
	return modeBits;
}

const char *
getSystemTempDir() {
	const char *temp_dir = getenv("PASSENGER_TEMP_DIR");
	if (temp_dir == NULL || *temp_dir == '\0') {
		temp_dir = getenv("PASSENGER_TMPDIR");
		if (temp_dir == NULL || *temp_dir == '\0') {
			temp_dir = "/tmp";
		}
	}
	return temp_dir;
}

void
makeDirTree(const string &path, const StaticString &mode, uid_t owner, gid_t group) {
	struct stat buf;
	vector<string> paths;
	vector<string>::reverse_iterator rit;
	string current = path;
	mode_t modeBits;
	int ret;
	
	if (stat(path.c_str(), &buf) == 0) {
		return;
	}
	
	modeBits = parseModeString(mode);
	
	/* Create a list of parent paths that don't exist. For example, given
	 * path == "/a/b/c/d/e" and that only /a exists, the list will become
	 * as follows:
	 *
	 * /a/b/c/d
	 * /a/b/c
	 * /a/b
	 */
	while (current != "/" && current != "." && getFileType(current) == FT_NONEXISTANT) {
		paths.push_back(current);
		current = extractDirName(current);
	}
	
	/* Now traverse the list in reverse order and create directories that don't exist. */
	for (rit = paths.rbegin(); rit != paths.rend(); rit++) {
		current = *rit;
		
		do {
			ret = mkdir(current.c_str(), modeBits);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			if (errno == EEXIST) {
				// Ignore error and don't chmod/chown.
				continue;
			} else {
				int e = errno;
				throw FileSystemException("Cannot create directory '" + current + "'",
					e, current);
			}
		}
		
		/* Chmod in order to override the umask. */
		do {
			ret = chmod(current.c_str(), modeBits);
		} while (ret == -1 && errno == EINTR);
		
		if (owner != USER_NOT_GIVEN && group != GROUP_NOT_GIVEN) {
			if (owner == USER_NOT_GIVEN) {
				owner = (uid_t) -1; // Don't let chown change file owner.
			}
			if (group == GROUP_NOT_GIVEN) {
				group = (gid_t) -1; // Don't let chown change file group.
			}
			do {
				ret = chown(current.c_str(), owner, group);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				char message[1024];
				int e = errno;
				
				snprintf(message, sizeof(message) - 1,
					"Cannot change the directory '%s' its UID to %lld and GID to %lld",
					current.c_str(), (long long) owner, (long long) group);
				message[sizeof(message) - 1] = '\0';
				throw FileSystemException(message, e, path);
			}
		}
	}
}

void
removeDirTree(const string &path) {
	char command[PATH_MAX + 30];
	int result;
	
	snprintf(command, sizeof(command), "chmod -R u+rwx \"%s\" 2>/dev/null", path.c_str());
	command[sizeof(command) - 1] = '\0';
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	
	snprintf(command, sizeof(command), "rm -rf \"%s\"", path.c_str());
	command[sizeof(command) - 1] = '\0';
	do {
		result = system(command);
	} while (result == -1 && errno == EINTR);
	if (result == -1) {
		char message[1024];
		int e = errno;
		
		snprintf(message, sizeof(message) - 1, "Cannot remove directory '%s'", path.c_str());
		message[sizeof(message) - 1] = '\0';
		throw FileSystemException(message, e, path);
	}
}

bool
verifyRailsDir(const string &dir, CachedFileStat *cstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/config/environment.rb");
	return fileExists(temp, cstat, throttleRate);
}

bool
verifyRackDir(const string &dir, CachedFileStat *cstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/config.ru");
	return fileExists(temp, cstat, throttleRate);
}

bool
verifyWSGIDir(const string &dir, CachedFileStat *cstat, unsigned int throttleRate) {
	string temp(dir);
	temp.append("/passenger_wsgi.py");
	return fileExists(temp, cstat, throttleRate);
}

void
prestartWebApps(const ResourceLocator &locator, const string &serializedprestartURLs) {
	/* Apache calls the initialization routines twice during startup, and
	 * as a result it starts two helper servers, where the first one exits
	 * after a short idle period. We want any prespawning requests to reach
	 * the second helper server, so we sleep for a short period before
	 * executing the prespawning scripts.
	 */
	syscalls::sleep(2);
	
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	vector<string> prestartURLs;
	vector<string>::const_iterator it;
	string prespawnScript = locator.getHelperScriptsDir() + "/prespawn";
	
	split(Base64::decode(serializedprestartURLs), '\0', prestartURLs);
	it = prestartURLs.begin();
	while (it != prestartURLs.end() && !this_thread::interruption_requested()) {
		if (it->empty()) {
			it++;
			continue;
		}
		
		pid_t pid;
		
		pid = fork();
		if (pid == 0) {
			long max_fds, i;
			int e;
			
			// Close all unnecessary file descriptors.
			max_fds = sysconf(_SC_OPEN_MAX);
			for (i = 3; i < max_fds; i++) {
				syscalls::close(i);
			}
			
			execlp(prespawnScript.c_str(),
				prespawnScript.c_str(),
				it->c_str(),
				(char *) 0);
			e = errno;
			fprintf(stderr, "Cannot execute '%s %s': %s (%d)\n",
				prespawnScript.c_str(), it->c_str(),
				strerror(e), e);
			fflush(stderr);
			_exit(1);
		} else if (pid == -1) {
			perror("fork()");
		} else {
			try {
				this_thread::restore_interruption si(di);
				this_thread::restore_syscall_interruption ssi(dsi);
				syscalls::waitpid(pid, NULL, 0);
			} catch (const thread_interrupted &) {
				syscalls::kill(SIGKILL, pid);
				syscalls::waitpid(pid, NULL, 0);
				throw;
			}
		}
		
		this_thread::restore_interruption si(di);
		this_thread::restore_syscall_interruption ssi(dsi);
		syscalls::sleep(1);
		it++;
	}
}

string
getHostName() {
	char hostname[HOST_NAME_MAX + 1];
	if (gethostname(hostname, sizeof(hostname)) == 0) {
		hostname[sizeof(hostname) - 1] = '\0';
		return hostname;
	} else {
		int e = errno;
		throw SystemException("Unable to query the system's host name", e);
	}
}

string
fillInMiddle(unsigned int max, const string &prefix, const string &middle, const string &postfix) {
	if (max <= prefix.size() + postfix.size()) {
		throw ArgumentException("Impossible to build string with the given size constraint.");
	}
	
	unsigned int fillSize = max - (prefix.size() + postfix.size());
	if (fillSize > middle.size()) {
		return prefix + middle + postfix;
	} else {
		return prefix + middle.substr(0, fillSize) + postfix;
	}
}

string
toHex(const StaticString &data) {
	string result(data.size() * 2, '\0');
	toHex(data, (char *) result.data());
	return result;
}

void
toHex(const StaticString &data, char *output) {
	static const char hex_chars[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'a', 'b', 'c', 'd', 'e', 'f'
	};
	const char *data_buf = data.c_str();
	string::size_type i;
	
	for (i = 0; i < data.size(); i++) {
		output[i * 2] = hex_chars[(unsigned char) data_buf[i] / 16];
		output[i * 2 + 1] = hex_chars[(unsigned char) data_buf[i] % 16];
	}
}

string
getSignalName(int sig) {
	switch (sig) {
	case SIGHUP:
		return "SIGHUP";
	case SIGINT:
		return "SIGINT";
	case SIGQUIT:
		return "SIGQUIT";
	case SIGILL:
		return "SIGILL";
	case SIGTRAP:
		return "SIGTRAP";
	case SIGABRT:
		return "SIGABRT";
	case SIGFPE:
		return "SIGFPE";
	case SIGKILL:
		return "SIGKILL";
	case SIGBUS:
		return "SIGBUS";
	case SIGSEGV:
		return "SIGSEGV";
	case SIGPIPE:
		return "SIGPIPE";
	case SIGALRM:
		return "SIGARLM";
	case SIGTERM:
		return "SIGTERM";
	case SIGUSR1:
		return "SIGUSR1";
	case SIGUSR2:
		return "SIGUSR2";
	default:
		return toString(sig);
	}
}

int
createUnixServer(const char *filename, unsigned int backlogSize, bool autoDelete) {
	struct sockaddr_un addr;
	int fd, ret;
	
	if (strlen(filename) > sizeof(addr.sun_path) - 1) {
		string message = "Cannot create Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		throw SystemException("Cannot create a Unix socket file descriptor", errno);
	}
	
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	if (autoDelete) {
		do {
			ret = unlink(filename);
		} while (ret == -1 && errno == EINTR);
	}
	
	try {
		ret = syscalls::bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot bind Unix socket '";
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	if (backlogSize == 0) {
		backlogSize = 1024;
	}
	try {
		ret = syscalls::listen(fd, backlogSize);
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message = "Cannot listen on Unix socket '";
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

int
connectToUnixServer(const char *filename) {
	int fd, ret;
	struct sockaddr_un addr;
	
	if (strlen(filename) > sizeof(addr.sun_path) - 1) {
		string message = "Cannot connect to Unix socket '";
		message.append(filename);
		message.append("': filename is too long.");
		throw RuntimeException(message);
	}
	
	fd = syscalls::socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		int e = errno;
		throw SystemException("Cannot create a Unix socket file descriptor", e);
	}
	
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
	
	try {
		ret = syscalls::connect(fd, (const sockaddr *) &addr, sizeof(addr));
	} catch (...) {
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	if (ret == -1) {
		int e = errno;
		string message("Cannot connect to Unix socket '");
		message.append(filename);
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

int
connectToTcpServer(const char *hostname, unsigned int port) {
	struct addrinfo hints, *res;
	int ret, e, fd;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(hostname, toString(port).c_str(), &hints, &res);
	if (ret != 0) {
		string message = "Cannot resolve IP address '";
		message.append(hostname);
		message.append(":");
		message.append(toString(port));
		message.append("': ");
		message.append(gai_strerror(ret));
		throw IOException(message);
	}
	
	try {
		fd = syscalls::socket(PF_INET, SOCK_STREAM, 0);
	} catch (...) {
		freeaddrinfo(res);
		throw;
	}
	if (fd == -1) {
		e = errno;
		freeaddrinfo(res);
		throw SystemException("Cannot create a TCP socket file descriptor", e);
	}
	
	try {
		ret = syscalls::connect(fd, res->ai_addr, res->ai_addrlen);
	} catch (...) {
		freeaddrinfo(res);
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw;
	}
	e = errno;
	freeaddrinfo(res);
	if (ret == -1) {
		string message = "Cannot connect to TCP socket '";
		message.append(hostname);
		message.append(":");
		message.append(toString(port));
		message.append("'");
		do {
			ret = close(fd);
		} while (ret == -1 && errno == EINTR);
		throw SystemException(message, e);
	}
	
	return fd;
}

} // namespace Passenger

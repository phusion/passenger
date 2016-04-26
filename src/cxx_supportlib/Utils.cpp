/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2015 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#include <boost/shared_array.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <libgen.h>
#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#ifdef __linux__
	#include <sys/syscall.h>
	#include <features.h>
#endif
#include <vector>
#include <FileDescriptor.h>
#include <ResourceLocator.h>
#include <Exceptions.h>
#include <Utils.h>
#include <Utils/CachedFileStat.hpp>
#include <Utils/StrIntUtils.h>
#include <Utils/IOUtils.h>

#ifndef HOST_NAME_MAX
	#if defined(_POSIX_HOST_NAME_MAX)
		#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
	#elif defined(_SC_HOST_NAME_MAX)
		#define HOST_NAME_MAX sysconf(_SC_HOST_NAME_MAX)
	#else
		#define HOST_NAME_MAX 255
	#endif
#endif
#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(__sun)
	// Introduced in Solaris 9. Let's hope nobody actually uses
	// a version that doesn't support this.
	#define HAS_CLOSEFROM
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

bool
fileExists(const StaticString &filename, CachedFileStat *cstat, boost::mutex *cstatMutex,
	unsigned int throttleRate)
{
	return getFileType(filename, cstat, cstatMutex, throttleRate) == FT_REGULAR;
}

FileType
getFileType(const StaticString &filename, CachedFileStat *cstat, boost::mutex *cstatMutex,
	unsigned int throttleRate)
{
	struct stat buf;
	int ret;

	if (cstat != NULL) {
		boost::unique_lock<boost::mutex> l;
		if (cstatMutex != NULL) {
			l = boost::unique_lock<boost::mutex>(*cstatMutex);
		}
		ret = cstat->stat(filename, &buf, throttleRate);
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
		fd.assign(open(filename.c_str(), options, permissions),
			__FILE__, __LINE__);
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
			writeExact(fd, contents);
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
resolveSymlink(const StaticString &path) {
	char buf[PATH_MAX];
	ssize_t size;

	size = readlink(path.c_str(), buf, sizeof(buf) - 1);
	if (size == -1) {
		if (errno == EINVAL) {
			return path;
		} else {
			int e = errno;
			string message = "Cannot resolve possible symlink '";
			message.append(path.c_str(), path.size());
			message.append("'");
			throw FileSystemException(message, e, path);
		}
	} else {
		buf[size] = '\0';
		if (buf[0] == '\0') {
			string message = "The file '";
			message.append(path.c_str(), path.size());
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
	DynamicBuffer pathCopy(path.size() + 1);
	memcpy(pathCopy.data, path.data(), path.size());
	pathCopy.data[path.size()] = '\0';
	return string(dirname(pathCopy.data));
}

StaticString
extractDirNameStatic(const StaticString &path) {
	if (path.empty()) {
		return StaticString(".", 1);
	}

	const char *data = path.data();
	const char *end = path.data() + path.size();

	// Ignore trailing '/' characters.
	while (end > data && end[-1] == '/') {
		end--;
	}
	if (end == data) {
		// Apparently the entire path consists of slashes.
		return StaticString("/", 1);
	}

	// Find last '/'.
	end--;
	while (end > data && *end != '/') {
		end--;
	}
	if (end == data) {
		if (*data == '/') {
			// '/' found, but it's the first character in the path.
			return StaticString("/", 1);
		} else {
			// No '/' found in path.
			return StaticString(".", 1);
		}
	} else {
		// '/' found and it's not the first character in path.
		// 'end' points to that '/' character.
		// Skip to first non-'/' character.
		while (end >= data && *end == '/') {
			end--;
		}
		if (end < data) {
			// The entire path consists of '/' characters.
			return StaticString("/", 1);
		} else {
			return StaticString(data, end - data + 1);
		}
	}
}

string
extractBaseName(const StaticString &path) {
	char *path_copy = strdup(path.c_str());
	string result_string = basename(path_copy);
	free(path_copy);
	return result_string;
}

string
escapeForXml(const StaticString &input) {
	string result(input.data(), input.size());
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

string
getProcessUsername(bool fallback) {
	struct passwd pwd, *result;
	long bufSize;
	shared_array<char> strings;

	// _SC_GETPW_R_SIZE_MAX is not a maximum:
	// http://tomlee.co/2012/10/problems-with-large-linux-unix-groups-and-getgrgid_r-getgrnam_r/
	bufSize = std::max<long>(1024 * 128, sysconf(_SC_GETPW_R_SIZE_MAX));
	strings.reset(new char[bufSize]);

	result = (struct passwd *) NULL;
	if (getpwuid_r(getuid(), &pwd, strings.get(), bufSize, &result) != 0) {
		result = (struct passwd *) NULL;
	}

	if (result == (struct passwd *) NULL || result->pw_name == NULL || result->pw_name[0] == '\0') {
		if (fallback) {
			snprintf(strings.get(), bufSize, "UID %lld", (long long) getuid());
			strings.get()[bufSize - 1] = '\0';
			return strings.get();
		} else {
			return string();
		}
	} else {
		return result->pw_name;
	}
}

string
getGroupName(gid_t gid) {
	struct group grp, *groupEntry;
	long bufSize;
	shared_array<char> strings;

	// _SC_GETGR_R_SIZE_MAX is not a maximum:
	// http://tomlee.co/2012/10/problems-with-large-linux-unix-groups-and-getgrgid_r-getgrnam_r/
	bufSize = std::max<long>(1024 * 128, sysconf(_SC_GETGR_R_SIZE_MAX));
	strings.reset(new char[bufSize]);

	groupEntry = (struct group *) NULL;
	if (getgrgid_r(gid, &grp, strings.get(), bufSize, &groupEntry) != 0) {
		groupEntry = (struct group *) NULL;
	}

	if (groupEntry == (struct group *) NULL) {
		return toString(gid);
	} else {
		return groupEntry->gr_name;
	}
}

gid_t
lookupGid(const string &groupName) {
	struct group grp, *groupEntry;
	long bufSize;
	shared_array<char> strings;

	// _SC_GETGR_R_SIZE_MAX is not a maximum:
	// http://tomlee.co/2012/10/problems-with-large-linux-unix-groups-and-getgrgid_r-getgrnam_r/
	bufSize = std::max<long>(1024 * 128, sysconf(_SC_GETGR_R_SIZE_MAX));
	strings.reset(new char[bufSize]);

	groupEntry = (struct group *) NULL;
	if (getgrnam_r(groupName.c_str(), &grp, strings.get(), bufSize, &groupEntry) != 0) {
		groupEntry = (struct group *) NULL;
	}

	if (groupEntry == (struct group *) NULL) {
		if (looksLikePositiveNumber(groupName)) {
			return atoi(groupName);
		} else {
			return (gid_t) -1;
		}
	} else {
		return groupEntry->gr_gid;
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
		} else if (clause.size() < 2 || (clause[0] != '+' && clause[1] != '=')) {
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
		case '+':
			for (string::size_type i = 1; i < clause.size(); i++) {
				switch (clause[i]) {
				case 't':
					modeBits |= S_ISVTX;
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

string
absolutizePath(const StaticString &path, const StaticString &workingDir) {
	vector<string> components;
	if (!startsWith(path, "/")) {
		if (workingDir.empty()) {
			char buffer[PATH_MAX];
			if (getcwd(buffer, sizeof(buffer)) == NULL) {
				int e = errno;
				throw SystemException("Unable to query current working directory", e);
			}
			split(buffer + 1, '/', components);
		} else {
			string absoluteWorkingDir = absolutizePath(workingDir);
			split(StaticString(absoluteWorkingDir.data() + 1, absoluteWorkingDir.size() - 1),
				'/', components);
		}
	}

	const char *begin = path.data();
	const char *end = path.data() + path.size();

	// Skip leading slashes.
	while (begin < end && *begin == '/') {
		begin++;
	}

	while (begin < end) {
		const char *next = (const char *) memchr(begin, '/', end - begin);
		if (next == NULL) {
			next = end;
		}

		StaticString component(begin, next - begin);
		if (component == "..") {
			if (!components.empty()) {
				components.pop_back();
			}
		} else if (component != ".") {
			components.push_back(component);
		}

		// Skip slashes until beginning of next path component.
		begin = next + 1;
		while (begin < end && *begin == '/') {
			begin++;
		}
	}

	string result;
	vector<string>::const_iterator c_it, c_end = components.end();
	for (c_it = components.begin(); c_it != c_end; c_it++) {
		result.append("/");
		result.append(*c_it);
	}
	if (result.empty()) {
		result = "/";
	}
	return result;
}

const char *
getSystemTempDir() {
	const char *temp_dir = getenv("TMPDIR");
	if (temp_dir == NULL || *temp_dir == '\0') {
		temp_dir = "/tmp";
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
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	const char *c_path = path.c_str();
	pid_t pid;

	pid = syscalls::fork();
	if (pid == 0) {
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull != -1) {
			dup2(devnull, 2);
		}
		closeAllFileDescriptors(2);
		execlp("chmod", "chmod", "-R", "u+rwx", c_path, (char * const) 0);
		perror("Cannot execute chmod");
		_exit(1);

	} else if (pid == -1) {
		int e = errno;
		throw SystemException("Cannot fork a new process", e);

	} else {
		this_thread::restore_interruption ri(di);
		this_thread::restore_syscall_interruption rsi(dsi);
		syscalls::waitpid(pid, NULL, 0);
	}

	pid = syscalls::fork();
	if (pid == 0) {
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		closeAllFileDescriptors(2);
		execlp("rm", "rm", "-rf", c_path, (char * const) 0);
		perror("Cannot execute rm");
		_exit(1);

	} else if (pid == -1) {
		int e = errno;
		throw SystemException("Cannot fork a new process", e);

	} else {
		this_thread::restore_interruption ri(di);
		this_thread::restore_syscall_interruption rsi(dsi);
		int status;
		if (syscalls::waitpid(pid, &status, 0) == -1 || status != 0) {
			throw RuntimeException("Cannot remove directory '" + path + "'");
		}
	}
}

void
prestartWebApps(const ResourceLocator &locator, const string &ruby,
	const vector<string> &prestartURLs)
{
	/* Apache calls the initialization routines twice during startup, and
	 * as a result it starts two watchdogs, where the first one exits
	 * after a short idle period. We want any prespawning requests to reach
	 * the second watchdog, so we sleep for a short period before
	 * executing the prespawning scripts.
	 */
	syscalls::sleep(2);

	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	vector<string>::const_iterator it;
	string prespawnScript = locator.getHelperScriptsDir() + "/prespawn";

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

			execlp(ruby.c_str(),
				ruby.c_str(),
				prespawnScript.c_str(),
				it->c_str(),
				(char *) 0);
			e = errno;
			fprintf(stderr, "Cannot execute '%s %s %s': %s (%d)\n",
				ruby.c_str(),
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

void
runAndPrintExceptions(const boost::function<void ()> &func, bool toAbort) {
	try {
		func();
	} catch (const boost::thread_interrupted &) {
		throw;
	} catch (const tracable_exception &e) {
		P_ERROR("Exception: " << e.what() << "\n" << e.backtrace());
		if (toAbort) {
			abort();
		}
	}
}

void
runAndPrintExceptions(const boost::function<void ()> &func) {
	runAndPrintExceptions(func, true);
}

string
getHostName() {
	long hostNameMax = HOST_NAME_MAX;
	if (hostNameMax < 255) {
		// https://bugzilla.redhat.com/show_bug.cgi?id=130733
		hostNameMax = 255;
	}

	string buf(hostNameMax + 1, '\0');
	if (gethostname(&buf[0], hostNameMax + 1) == 0) {
		buf[hostNameMax] = '\0';
		return string(buf.c_str());
	} else {
		int e = errno;
		throw SystemException("Unable to query the system's host name", e);
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
	#ifdef SIGEMT
		case SIGEMT:
			return "SIGEMT";
	#endif
	#ifdef SIGINFO
		case SIGINFO:
			return "SIGINFO";
	#endif
	default:
		return toString(sig);
	}
}

void
resetSignalHandlersAndMask() {
	struct sigaction action;
	action.sa_handler = SIG_DFL;
	action.sa_flags   = SA_RESTART;
	sigemptyset(&action.sa_mask);
	sigaction(SIGHUP,  &action, NULL);
	sigaction(SIGINT,  &action, NULL);
	sigaction(SIGQUIT, &action, NULL);
	sigaction(SIGILL,  &action, NULL);
	sigaction(SIGTRAP, &action, NULL);
	sigaction(SIGABRT, &action, NULL);
	#ifdef SIGEMT
		sigaction(SIGEMT,  &action, NULL);
	#endif
	sigaction(SIGFPE,  &action, NULL);
	sigaction(SIGBUS,  &action, NULL);
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGSYS,  &action, NULL);
	sigaction(SIGPIPE, &action, NULL);
	sigaction(SIGALRM, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGURG,  &action, NULL);
	sigaction(SIGSTOP, &action, NULL);
	sigaction(SIGTSTP, &action, NULL);
	sigaction(SIGCONT, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);
	#ifdef SIGINFO
		sigaction(SIGINFO, &action, NULL);
	#endif
	sigaction(SIGUSR1, &action, NULL);
	sigaction(SIGUSR2, &action, NULL);

	// We reset the signal mask after resetting the signal handlers,
	// because prior to calling resetSignalHandlersAndMask(), the
	// process might be blocked on some signals. We want those signals
	// to be processed after installing the new signal handlers
	// so that bugs like https://github.com/phusion/passenger/pull/97
	// can be prevented.

	sigset_t signal_set;
	int ret;

	sigemptyset(&signal_set);
	do {
		ret = sigprocmask(SIG_SETMASK, &signal_set, NULL);
	} while (ret == -1 && errno == EINTR);
}

void
disableMallocDebugging() {
	unsetenv("MALLOC_FILL_SPACE");
	unsetenv("MALLOC_PROTECT_BEFORE");
	unsetenv("MallocGuardEdges");
	unsetenv("MallocScribble");
	unsetenv("MallocPreScribble");
	unsetenv("MallocCheckHeapStart");
	unsetenv("MallocCheckHeapEach");
	unsetenv("MallocCheckHeapAbort");
	unsetenv("MallocBadFreeAbort");
	unsetenv("MALLOC_CHECK_");

	const char *libs = getenv("DYLD_INSERT_LIBRARIES");
	if (libs != NULL && strstr(libs, "/usr/lib/libgmalloc.dylib")) {
		string newLibs = libs;
		string::size_type pos = newLibs.find("/usr/lib/libgmalloc.dylib");
		size_t len = strlen("/usr/lib/libgmalloc.dylib");

		// Erase all leading ':' too.
		while (pos > 0 && newLibs[pos - 1] == ':') {
			pos--;
			len++;
		}
		// Erase all trailing ':' too.
		while (pos + len < newLibs.size() && newLibs[pos + len] == ':') {
			len++;
		}

		newLibs.erase(pos, len);
		if (newLibs.empty()) {
			unsetenv("DYLD_INSERT_LIBRARIES");
		} else {
			setenv("DYLD_INSERT_LIBRARIES", newLibs.c_str(), 1);
		}
	}
}

int
runShellCommand(const StaticString &command) {
	pid_t pid = fork();
	if (pid == 0) {
		resetSignalHandlersAndMask();
		disableMallocDebugging();
		closeAllFileDescriptors(2);
		execlp("/bin/sh", "/bin/sh", "-c", command.data(), (char * const) 0);
		_exit(1);
	} else if (pid == -1) {
		return -1;
	} else {
		int status;
		if (waitpid(pid, &status, 0) == -1) {
			return -1;
		} else {
			return status;
		}
	}
}

string
runCommandAndCaptureOutput(const char **command) {
	pid_t pid;
	int e;
	Pipe p;

	p = createPipe(__FILE__, __LINE__);

	this_thread::disable_syscall_interruption dsi;
	pid = syscalls::fork();
	if (pid == 0) {
		// Make ps nicer, we want to have as little impact on the rest
		// of the system as possible while collecting the metrics.
		int prio = getpriority(PRIO_PROCESS, getpid());
		prio++;
		if (prio > 20) {
			prio = 20;
		}
		setpriority(PRIO_PROCESS, getpid(), prio);

		dup2(p[1], 1);
		close(p[0]);
		close(p[1]);
		closeAllFileDescriptors(2);
		execvp(command[0], (char * const *) command);
		_exit(1);
	} else if (pid == -1) {
		e = errno;
		throw SystemException("Cannot fork() a new process", e);
	} else {
		bool done = false;
		string result;

		p[1].close();
		while (!done) {
			char buf[1024 * 4];
			ssize_t ret;

			try {
				this_thread::restore_syscall_interruption rsi(dsi);
				ret = syscalls::read(p[0], buf, sizeof(buf));
			} catch (const thread_interrupted &) {
				syscalls::kill(SIGKILL, pid);
				syscalls::waitpid(pid, NULL, 0);
				throw;
			}
			if (ret == -1) {
				e = errno;
				syscalls::kill(SIGKILL, pid);
				syscalls::waitpid(pid, NULL, 0);
				throw SystemException(string("Cannot read output from the '") +
					command[0] + "' command", e);
			}
			done = ret == 0;
			result.append(buf, ret);
		}
		p[0].close();
		syscalls::waitpid(pid, NULL, 0);

		if (result.empty()) {
			throw RuntimeException(string("The '") + command[1] +
				"' command failed");
		} else {
			return result;
		}
	}
}

#ifdef __APPLE__
	// http://www.opensource.apple.com/source/Libc/Libc-825.26/sys/fork.c
	// This bypasses atfork handlers.
	extern "C" {
		extern pid_t __fork(void);
	}
#endif

pid_t
asyncFork() {
	#if defined(__linux__)
		#if defined(SYS_fork)
			return (pid_t) syscall(SYS_fork);
		#else
			return syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
		#endif
	#elif defined(__APPLE__)
		return __fork();
	#else
		return fork();
	#endif
}

// Async-signal safe way to get the current process's hard file descriptor limit.
static int
getFileDescriptorLimit() {
	long long sysconfResult = sysconf(_SC_OPEN_MAX);

	struct rlimit rl;
	long long rlimitResult;
	if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
		rlimitResult = 0;
	} else {
		rlimitResult = (long long) rl.rlim_max;
	}

	long result;
	// OS X 10.9 returns LLONG_MAX. It doesn't make sense
	// to use that result so we limit ourselves to the
	// sysconf result.
	if (rlimitResult >= INT_MAX || sysconfResult > rlimitResult) {
		result = sysconfResult;
	} else {
		result = rlimitResult;
	}

	if (result < 0) {
		// Unable to query the file descriptor limit.
		result = 9999;
	} else if (result < 2) {
		// The calls reported broken values.
		result = 2;
	}
	return result;
}

// Async-signal safe function to get the highest file
// descriptor that the process is currently using.
// See also http://stackoverflow.com/questions/899038/getting-the-highest-allocated-file-descriptor
static int
getHighestFileDescriptor(bool asyncSignalSafe) {
#if defined(F_MAXFD)
	int ret;

	do {
		ret = fcntl(0, F_MAXFD);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		ret = getFileDescriptorLimit();
	}
	return ret;

#else
	int p[2], ret, flags;
	pid_t pid = -1;
	int result = -1;

	/* Since opendir() may not be async signal safe and thus may lock up
	 * or crash, we use it in a child process which we kill if we notice
	 * that things are going wrong.
	 */

	// Make a pipe.
	p[0] = p[1] = -1;
	do {
		ret = pipe(p);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		goto done;
	}

	// Make the read side non-blocking.
	do {
		flags = fcntl(p[0], F_GETFL);
	} while (flags == -1 && errno == EINTR);
	if (flags == -1) {
		goto done;
	}
	do {
		fcntl(p[0], F_SETFL, flags | O_NONBLOCK);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1) {
		goto done;
	}

	if (asyncSignalSafe) {
		do {
			pid = asyncFork();
		} while (pid == -1 && errno == EINTR);
	} else {
		do {
			pid = fork();
		} while (pid == -1 && errno == EINTR);
	}

	if (pid == 0) {
		// Don't close p[0] here or it might affect the result.

		resetSignalHandlersAndMask();

		struct sigaction action;
		action.sa_handler = _exit;
		action.sa_flags   = SA_RESTART;
		sigemptyset(&action.sa_mask);
		sigaction(SIGSEGV, &action, NULL);
		sigaction(SIGPIPE, &action, NULL);
		sigaction(SIGBUS, &action, NULL);
		sigaction(SIGILL, &action, NULL);
		sigaction(SIGFPE, &action, NULL);
		sigaction(SIGABRT, &action, NULL);

		DIR *dir = NULL;
		#ifdef __APPLE__
			/* /dev/fd can always be trusted on OS X. */
			dir = opendir("/dev/fd");
		#else
			/* On FreeBSD and possibly other operating systems, /dev/fd only
			 * works if fdescfs is mounted. If it isn't mounted then /dev/fd
			 * still exists but always returns [0, 1, 2] and thus can't be
			 * trusted. If /dev and /dev/fd are on different filesystems
			 * then that probably means fdescfs is mounted.
			 */
			struct stat dirbuf1, dirbuf2;
			if (stat("/dev", &dirbuf1) == -1
			 || stat("/dev/fd", &dirbuf2) == -1) {
				_exit(1);
			}
			if (dirbuf1.st_dev != dirbuf2.st_dev) {
				dir = opendir("/dev/fd");
			}
		#endif
		if (dir == NULL) {
			dir = opendir("/proc/self/fd");
			if (dir == NULL) {
				_exit(1);
			}
		}

		struct dirent *ent;
		union {
			int highest;
			char data[sizeof(int)];
		} u;
		u.highest = -1;

		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] != '.') {
				int number = atoi(ent->d_name);
				if (number > u.highest) {
					u.highest = number;
				}
			}
		}
		if (u.highest != -1) {
			ssize_t ret, written = 0;
			do {
				ret = write(p[1], u.data + written, sizeof(int) - written);
				if (ret == -1) {
					_exit(1);
				}
				written += ret;
			} while (written < (ssize_t) sizeof(int));
		}
		closedir(dir);
		_exit(0);

	} else if (pid == -1) {
		goto done;

	} else {
		close(p[1]); // Do not retry on EINTR: http://news.ycombinator.com/item?id=3363819
		p[1] = -1;

		union {
			int highest;
			char data[sizeof(int)];
		} u;
		ssize_t ret, bytesRead = 0;
		struct pollfd pfd;
		pfd.fd = p[0];
		pfd.events = POLLIN;

		do {
			do {
				// The child process must finish within 30 ms, otherwise
				// we might as well query sysconf.
				ret = poll(&pfd, 1, 30);
			} while (ret == -1 && errno == EINTR);
			if (ret <= 0) {
				goto done;
			}

			do {
				ret = read(p[0], u.data + bytesRead, sizeof(int) - bytesRead);
			} while (ret == -1 && errno == EINTR);
			if (ret == -1) {
				if (errno != EAGAIN) {
					goto done;
				}
			} else if (ret == 0) {
				goto done;
			} else {
				bytesRead += ret;
			}
		} while (bytesRead < (ssize_t) sizeof(int));

		result = u.highest;
		goto done;
	}

done:
	// Do not retry on EINTR: http://news.ycombinator.com/item?id=3363819
	if (p[0] != -1) {
		close(p[0]);
	}
	if (p[1] != -1) {
		close(p[1]);
	}
	if (pid != -1) {
		do {
			ret = kill(pid, SIGKILL);
		} while (ret == -1 && errno == EINTR);
		do {
			ret = waitpid(pid, NULL, 0);
		} while (ret == -1 && errno == EINTR);
	}

	if (result == -1) {
		result = getFileDescriptorLimit();
	}
	return result;
#endif
}

void
closeAllFileDescriptors(int lastToKeepOpen, bool asyncSignalSafe) {
	#if defined(F_CLOSEM)
		int ret;
		do {
			ret = fcntl(lastToKeepOpen + 1, F_CLOSEM);
		} while (ret == -1 && errno == EINTR);
		if (ret != -1) {
			return;
		}
	#elif defined(HAS_CLOSEFROM)
		closefrom(lastToKeepOpen + 1);
		return;
	#endif

	for (int i = getHighestFileDescriptor(asyncSignalSafe); i > lastToKeepOpen; i--) {
		/* Even though we normally shouldn't retry on EINTR
		 * (http://news.ycombinator.com/item?id=3363819)
		 * it's okay to do that here because because this function
		 * may only be called in a single-threaded environment.
		 */
		int ret;
		do {
			ret = close(i);
		} while (ret == -1 && errno == EINTR);
	}
}

void
breakpoint() {
	// No-op.
}

} // namespace Passenger

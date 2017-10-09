/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#include <cstdio>
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
#include <ProcessManagement/Spawn.h>
#include <ProcessManagement/Utils.h>
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

namespace Passenger {

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
	gid_t group, bool overwrite, const char *callerFile, unsigned int callerLine)
{
	FileDescriptor fd;
	int ret, e, options;

	options = O_WRONLY | O_CREAT | O_TRUNC;
	if (!overwrite) {
		options |= O_EXCL;
	}
	do {
		fd.assign(open(filename.c_str(), options, permissions),
			(callerFile == NULL) ? __FILE__ : callerFile,
			(callerLine == 0) ? __LINE__ : callerLine);
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

static void
redirectStderrToDevNull() {
	int devnull = open("/dev/null", O_RDONLY);
	if (devnull > 2) {
		dup2(devnull, 2);
		close(devnull);
	}
}

void
removeDirTree(const string &path) {
	{
		const char *command[] = {
			"chmod",
			"-R",
			"u+rwx",
			path.c_str(),
			NULL
		};
		SubprocessInfo info;
		runCommand(command, info, true, true, redirectStderrToDevNull);
	}
	{
		const char *command[] = {
			"rm",
			"-rf",
			path.c_str(),
			NULL
		};
		SubprocessInfo info;
		runCommand(command, info, true, true, redirectStderrToDevNull);
		if (info.status != 0 && info.status != -2) {
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

	vector<string>::const_iterator it;
	string prespawnScript = locator.getHelperScriptsDir() + "/prespawn";

	it = prestartURLs.begin();
	while (it != prestartURLs.end() && !boost::this_thread::interruption_requested()) {
		if (it->empty()) {
			it++;
			continue;
		}

		const char *command[] = {
			ruby.c_str(),
			prespawnScript.c_str(),
			it->c_str(),
			NULL
		};
		SubprocessInfo info;
		runCommand(command, info);

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
breakpoint() {
	// No-op.
}

} // namespace Passenger

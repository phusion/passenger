/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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
#include <StrIntTools/StrIntUtils.h>
#include <IOTools/IOUtils.h>

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
escapeShell(const StaticString &value) {
	if (value.empty()) {
		return "''";
	}

	const char *pos = value.data();
	const char *end = value.data() + value.size();
	string result;

	result.reserve(value.size() * 1.5);

	while (pos < end) {
		char ch = *pos;
		if (ch == '\n') {
			// It is not possible to escape a newline with a backslash.
			// This is because a backslash + newline combination
			// is treated to mean a line continuation
			result.append("'\n'", 3);
		} else {
			bool allowed =
				(ch >= 'A' && ch <= 'Z')
				|| (ch >= 'a' && ch <= 'z')
				|| (ch >= '0' && ch <= '9')
				|| ch == '_'
				|| ch == '-'
				|| ch == '.'
				|| ch == ','
				|| ch == ':'
				|| ch == '/'
				|| ch == '@';
			if (!allowed) {
				result.append(1, '\\');
			}
			result.append(1, ch);
		}
		pos++;
	}

	return result;
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

const char *
getSystemTempDir() {
	const char *temp_dir = getenv("TMPDIR");
	if (temp_dir == NULL || *temp_dir == '\0') {
		temp_dir = "/tmp";
	}
	return temp_dir;
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

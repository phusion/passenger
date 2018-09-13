/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
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

// A helper app that reads from an arbitrary file.
// Its main reason for existance is to allow root processes (such as the Core)
// to read from arbitrary files in a way that's safe from symlink and other
// kinds of attacks. See the documentation for safeReadFile()
// to learn more about the different types of attacks.
//
// file-read-helper is used when the caller cannot use safeReadFile(),
// e.g. when the following two conditions hold at the same time:
//
//  1. The caller does not have control over the safety of the parent
//     directories leading to the file.
//  2. The caller cannot choose not to disclose the contents of the file.
//
// file-read-helper MUST be used in combination with exec-helper in order
// to lower its privilege, otherwise no protection is provided.

#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <string>
#include <algorithm>
#include <limits>

#include <fcntl.h>
#include <unistd.h>

#include <Constants.h>
#include <ProcessManagement/Utils.h>
#include <IOTools/IOUtils.h>
#include <Utils/OptionParsing.h>

namespace Passenger {
namespace FileReadHelper {

using namespace std;


struct Options {
	size_t limit;
	int programArgStart;

	Options()
		: limit(std::numeric_limits<size_t>::max()),
		  programArgStart(2)
		{ }
};

static void
usage() {
	// ....|---------------Keep output within standard terminal width (80 chars)------------|
	printf("Usage: " AGENT_EXE " file-read-helper [OPTIONS...] <PATH>\n");
	printf("Reads the given file with O_NONBLOCK.\n");
	printf("\n");
	printf("Options:\n");
	printf("  --limit <SIZE>  Limit the number of bytes read (default: unlimited).\n");
	printf("  --help          Show this help message.\n");
}

static bool
parseOption(int argc, const char *argv[], int &i, Options &options) {
	OptionParser p(usage);

	if (p.isValueFlag(argc, i, argv[i], '\0', "--limit")) {
		options.limit = atoi(argv[i + 1]);
		i += 2;
	} else {
		return false;
	}
	return true;
}

static bool
parseOptions(int argc, const char *argv[], Options &options) {
	OptionParser p(usage);
	int i = 2;

	while (i < argc) {
		if (parseOption(argc, argv, i, options)) {
			continue;
		} else if (p.isFlag(argv[i], 'h', "--help")) {
			usage();
			exit(0);
		} else if (*argv[i] == '-') {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s file-read-helper --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		} else {
			options.programArgStart = i;
			return true;
		}
	}

	return true;
}

int
fileReadHelperMain(int argc, char *argv[]) {
	if (argc < 3) {
		usage();
		exit(1);
	}

	Options options;
	if (!parseOptions(argc, (const char **) argv, options)) {
		fprintf(stderr, "Error parsing arguments.\n");
		usage();
		exit(1);
	}

	if (argc != options.programArgStart + 1) {
		fprintf(stderr, "ERROR: no file path given. Please type "
			"'%s file-read-helper --help' for usage.\n", argv[0]);
		exit(1);
	}

	if (geteuid() == 0) {
		fprintf(stderr, "ERROR: file-read-helper cannot be run with root"
			" privileges. Please use in combination with exec-helper.\n");
		exit(1);
	}

	resetSignalHandlersAndMask();
	disableMallocDebugging();

	const char *path = argv[options.programArgStart];
	int fd;
	do {
		fd = open(path, O_RDONLY | O_NONBLOCK);
	} while (fd == -1 && errno == EINTR);
	if (fd == -1) {
		int e = errno;
		fprintf(stderr, "Error opening %s for reading: %s (errno=%d)\n",
			path, strerror(e), e);
		exit(1);
	}

	size_t totalRead = 0;
	char buf[1024 * 16];

	while (totalRead < options.limit) {
		ssize_t ret;
		do {
			ret = read(fd, buf,
				std::min<size_t>(sizeof(buf), options.limit - totalRead));
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			int e = errno;
			fprintf(stderr, "Error reading from %s: %s (errno=%d)\n",
				path, strerror(e), e);
			exit(1);
		} else if (ret == 0) {
			break;
		} else {
			totalRead += ret;
			writeExact(1, StaticString(buf, ret));
		}
	}

	return 0;
}


} // namespace FileReadHelper
} // namespace Passenger


int
fileReadHelperMain(int argc, char *argv[]) {
	return Passenger::FileReadHelper::fileReadHelperMain(argc, argv);
}

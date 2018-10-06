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

#include <sys/types.h>
#include <sys/param.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <string>
#include <boost/scoped_array.hpp>

#include <Constants.h>
#include <ProcessManagement/Utils.h>
#include <Utils/OptionParsing.h>
#include <StrIntTools/StrIntUtils.h>

namespace Passenger {
namespace ExecHelper {

using namespace std;


struct Options {
	string user;
	int programArgStart;

	Options()
		: programArgStart(2)
		{ }
};

static void
usage() {
	// ....|---------------Keep output within standard terminal width (80 chars)------------|
	printf("Usage: " AGENT_EXE " exec-helper [OPTIONS...] <PROGRAM> [ARGS...]\n");
	printf("Executes the given program under a specific environment.\n");
	printf("\n");
	printf("Options:\n");
	printf("  --user <USER>   Execute as the given user. The GID will be set to the\n");
	printf("                  user's primary group. Supplementary groups will also\n");
	printf("                  be set.\n");
	printf("  --help          Show this help message.\n");
}

static bool
parseOption(int argc, const char *argv[], int &i, Options &options) {
	OptionParser p(usage);

	if (p.isValueFlag(argc, i, argv[i], '\0', "--user")) {
		options.user = argv[i + 1];
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
				"'%s exec-helper --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		} else {
			options.programArgStart = i;
			return true;
		}
	}

	return true;
}

static string
describeCommand(int argc, const char *argv[], const Options &options) {
	string result = "'";
	result.append(argv[options.programArgStart]);
	result.append("'");

	if (argc > options.programArgStart + 1) {
		result.append(" (with params '");

		int i = options.programArgStart + 1;
		while (i < argc) {
			if (i != options.programArgStart + 1) {
				result.append(" ");
			}
			result.append(argv[i]);
			i++;
		}

		result.append("')");
	}

	return result;
}

static void
reportGetpwuidError(const string &user, int e) {
	if (e == 0) {
		fprintf(stderr,
			"ERROR: Cannot lookup up system user database entry for user '%s':"
			" user does not exist\n", user.c_str());
	} else {
		fprintf(stderr,
			"ERROR: Cannot lookup up system user database entry for user '%s':"
			" %s (errno=%d)\n",
			user.c_str(), strerror(e), e);
	}
}

static void
lookupUserGroup(const string &user, uid_t *uid, struct passwd **userInfo, gid_t *gid) {
	errno = 0;
	*userInfo = getpwnam(user.c_str());
	if (*userInfo == NULL) {
		if (looksLikePositiveNumber(user)) {
			int e = errno;
			fprintf(stderr,
				"Warning: error looking up system user database"
				" entry for user '%s': %s (errno=%d)\n",
				user.c_str(), strerror(e), e);
			*uid = (uid_t) atoi(user.c_str());
			*userInfo = getpwuid(*uid);
			if (*userInfo == NULL) {
				reportGetpwuidError(user, errno);
				exit(1);
			} else {
				*gid = (*userInfo)->pw_gid;
			}
		} else {
			reportGetpwuidError(user, errno);
			exit(1);
		}
	} else {
		*uid = (*userInfo)->pw_uid;
		*gid = (*userInfo)->pw_gid;
	}
}

static void
switchGroup(uid_t uid, const struct passwd *userInfo, gid_t gid) {
	if (userInfo != NULL) {
		bool setgroupsCalled = false;

		#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
			#ifdef __APPLE__
				int groups[1024];
				int ngroups = sizeof(groups) / sizeof(int);
			#else
				gid_t groups[1024];
				int ngroups = sizeof(groups) / sizeof(gid_t);
			#endif
			boost::scoped_array<gid_t> gidset;

			int ret = getgrouplist(userInfo->pw_name, gid,
				groups, &ngroups);
			if (ret == -1) {
				int e = errno;
				fprintf(stderr, "ERROR: getgrouplist(%s, %d) failed: %s (errno=%d)\n",
					userInfo->pw_name, (int) gid, strerror(e), e);
				exit(1);
			}

			if (ngroups <= NGROUPS_MAX) {
				setgroupsCalled = true;
				gidset.reset(new gid_t[ngroups]);
				for (int i = 0; i < ngroups; i++) {
					gidset[i] = groups[i];
				}
				if (setgroups(ngroups, gidset.get()) == -1) {
					int e = errno;
					fprintf(stderr, "ERROR: setgroups(%d, ...) failed: %s (errno=%d)\n",
						ngroups, strerror(e), e);
					exit(1);
				}
			}
		#endif

		if (!setgroupsCalled && initgroups(userInfo->pw_name, gid) == -1) {
			int e = errno;
			fprintf(stderr, "ERROR: initgroups(%s, %d) failed: %s (errno=%d)\n",
				userInfo->pw_name, (int) gid, strerror(e), e);
			exit(1);
		}
	}

	if (setgid(gid) == -1) {
		int e = errno;
		fprintf(stderr, "ERROR: setgid(%d) failed: %s (errno=%d)\n",
			(int) gid, strerror(e), e);
		exit(1);
	}
}

static void
switchUser(uid_t uid, const struct passwd *userInfo) {
	if (setuid(uid) == -1) {
		int e = errno;
		fprintf(stderr, "setuid(%d) failed: %s (errno=%d)\n",
			(int) uid, strerror(e), e);
		exit(1);
	}
	if (userInfo != NULL) {
		setenv("USER", userInfo->pw_name, 1);
		setenv("LOGNAME", userInfo->pw_name, 1);
		setenv("SHELL", userInfo->pw_shell, 1);
		setenv("HOME", userInfo->pw_dir, 1);
	} else {
		unsetenv("USER");
		unsetenv("LOGNAME");
		unsetenv("SHELL");
		unsetenv("HOME");
	}
}

int
execHelperMain(int argc, char *argv[]) {
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

	resetSignalHandlersAndMask();
	disableMallocDebugging();

	if (!options.user.empty()) {
		struct passwd *userInfo;
		uid_t uid;
		gid_t gid;

		lookupUserGroup(options.user, &uid, &userInfo, &gid);
		switchGroup(uid, userInfo, gid);
		switchUser(uid, userInfo);
	}

	execvp(argv[options.programArgStart],
		(char * const *) &argv[options.programArgStart]);
	int e = errno;
	fprintf(stderr, "ERROR: unable to execute %s: %s (errno=%d)\n",
		describeCommand(argc, (const char **) argv, options).c_str(),
		strerror(e),
		e);
	return 1;
}


} // namespace ExecHelper
} // namespace Passenger


int
execHelperMain(int argc, char *argv[]) {
	return Passenger::ExecHelper::execHelperMain(argc, argv);
}

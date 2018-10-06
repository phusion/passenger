/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2018 Phusion Holding B.V.
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

/*
 * For an introduction see SpawningKit's README.md, section "The SpawnEnvSetupper".
 */

#include <oxt/initialize.hpp>
#include <oxt/backtrace.hpp>
#include <boost/scoped_array.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>

#include <jsoncpp/json.h>
#include <adhoc_lve.h>

#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Context.h>
#include <ProcessManagement/Spawn.h>
#include <FileTools/FileManip.h>
#include <FileTools/PathManip.h>
#include <SystemTools/UserDatabase.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>
#include <Core/SpawningKit/Handshake/WorkDir.h>
#include <Core/SpawningKit/Exceptions.h>

using namespace std;
using namespace Passenger;

extern "C" {
	extern char **environ;
}


namespace Passenger {
namespace SpawnEnvSetupper {

	enum Mode {
		BEFORE_MODE,
		AFTER_MODE
	};

	struct Context {
		string workDir;
		Mode mode;
		Json::Value args;
		SpawningKit::JourneyStep step;
	};

} // namespace SpawnEnvSetupper
} // namespace Passenger

using namespace Passenger::SpawnEnvSetupper;


static Json::Value
readArgsJson(const string &workDir) {
	Json::Reader reader;
	Json::Value result;
	string contents = unsafeReadFile(workDir + "/args.json");
	if (reader.parse(contents, result)) {
		return result;
	} else {
		P_CRITICAL("Cannot parse " << workDir << "/args.json: "
			<< reader.getFormattedErrorMessages());
		exit(1);
		// Never reached
		return Json::Value();
	}
}

static void
initializeLogLevel(const Json::Value &args) {
	if (args.isMember("log_level")) {
		LoggingKit::setLevel(LoggingKit::Level(args["log_level"].asInt()));
	}
}

static bool
tryWriteFile(const StaticString &path, const StaticString &value) {
	try {
		createFile(path.c_str(), value);
		return true;
	} catch (const FileSystemException &e) {
		fprintf(stderr, "Warning: %s\n", e.what());
		return false;
	}
}

static void
recordJourneyStepBegin(const Context &context,
	SpawningKit::JourneyStep step, SpawningKit::JourneyStepState state)
{
	string stepString = journeyStepToStringLowerCase(step);
	string stepDir = context.workDir + "/response/steps/" + stepString;
	tryWriteFile(stepDir + "/state", SpawningKit::journeyStepStateToString(state));
	tryWriteFile(stepDir + "/begin_time_monotonic", doubleToString(
		SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_10MSEC>() / 1000000.0));
}

static void
recordJourneyStepEnd(const Context &context,
	SpawningKit::JourneyStep step, SpawningKit::JourneyStepState state)
{
	string stepString = journeyStepToStringLowerCase(step);
	string stepDir = context.workDir + "/response/steps/" + stepString;
	tryWriteFile(stepDir + "/state", SpawningKit::journeyStepStateToString(state));
	if (!fileExists(stepDir + "/begin_time") && !fileExists(stepDir + "/begin_time_monotonic")) {
		tryWriteFile(stepDir + "/begin_time_monotonic", doubleToString(
			SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_10MSEC>() / 1000000.0));
	}
	tryWriteFile(stepDir + "/end_time_monotonic", doubleToString(
		SystemTime::getMonotonicUsecWithGranularity<SystemTime::GRAN_10MSEC>() / 1000000.0));
}

static void
recordErrorCategory(const string &workDir, SpawningKit::ErrorCategory category) {
	string path = workDir + "/response/error/category";
	tryWriteFile(path, errorCategoryToString(category));
}

static void
recordAdvancedProblemDetails(const string &workDir, const string &message) {
	string path = workDir + "/response/error/advanced_problem_details";
	tryWriteFile(path, message);
}

static void
recordErrorSummary(const string &workDir, const string &message,
	bool isAlsoAdvancedProblemDetails)
{
	string path = workDir + "/response/error/summary";
	tryWriteFile(path, message);
	if (isAlsoAdvancedProblemDetails) {
		recordAdvancedProblemDetails(workDir, message);
	}
}

static void
recordAndPrintErrorSummary(const string &workDir, const string &message,
	bool isAlsoAdvancedProblemDetails)
{
	fprintf(stderr, "Error: %s\n", message.c_str());
	recordErrorSummary(workDir, message, isAlsoAdvancedProblemDetails);
}

static void
recordProblemDescriptionHTML(const string &workDir, const string &message) {
	string path = workDir + "/response/error/problem_description.html";
	tryWriteFile(path, message);
}

static void
recordSolutionDescriptionHTML(const string &workDir, const string &message) {
	string path = workDir + "/response/error/solution_description.html";
	tryWriteFile(path, message);
}

static void
reopenStdout(int fd) {
	dup2(fd, STDOUT_FILENO);
}

static void
dumpEnvvars(const string &workDir) {
	FILE *f = fopen((workDir + "/envdump/envvars").c_str(), "w");
	if (f == NULL) {
		fprintf(stderr, "Warning: cannot open %s/envdump/envvars for writing\n",
			workDir.c_str());
		return;
	}

	const char *command[] = {
		"env",
		NULL
	};
	SubprocessInfo info;
	runCommand(command, info, true, true,
		boost::bind(reopenStdout, fileno(f)));
	fclose(f);
}

static void
dumpUserInfo(const string &workDir) {
	FILE *f = fopen((workDir + "/envdump/user_info").c_str(), "w");
	if (f == NULL) {
		fprintf(stderr, "Warning: cannot open %s/envdump/user_info for writing\n",
			workDir.c_str());
		return;
	}

	const char *command[] = {
		"id",
		NULL
	};
	SubprocessInfo info;
	runCommand(command, info, true, true,
		boost::bind(reopenStdout, fileno(f)));
	fclose(f);
}

static void
dumpUlimits(const string &workDir) {
	FILE *f = fopen((workDir + "/envdump/ulimits").c_str(), "w");
	if (f == NULL) {
		fprintf(stderr, "Warning: cannot open %s/envdump/ulimits for writing\n",
			workDir.c_str());
		return;
	}

	// On Linux, ulimit is a shell builtin and not a command.
	const char *command[] = {
		"/bin/sh",
		"-c",
		"ulimit -a",
		NULL
	};
	SubprocessInfo info;
	runCommand(command, info, true, true,
		boost::bind(reopenStdout, fileno(f)));
	fclose(f);
}

static void
dumpAllEnvironmentInfo(const string &workDir) {
	dumpEnvvars(workDir);
	dumpUserInfo(workDir);
	dumpUlimits(workDir);
}

static bool
setUlimits(const Json::Value &args) {
	if (!args.isMember("file_descriptor_ulimit")) {
		return false;
	}

	rlim_t fdLimit = (rlim_t) args["file_descriptor_ulimit"].asUInt();
	struct rlimit limit;
	int ret;

	limit.rlim_cur = fdLimit;
	limit.rlim_max = fdLimit;
	do {
		ret = setrlimit(RLIMIT_NOFILE, &limit);
	} while (ret == -1 && errno == EINTR);

	if (ret == -1) {
		int e = errno;
		fprintf(stderr, "Error: unable to set file descriptor ulimit to %u: %s (errno=%d)",
			(unsigned int) fdLimit, strerror(e), e);
	}

	return ret != -1;
}

static bool
canSwitchUser(const Json::Value &args) {
	return args.isMember("user") && geteuid() == 0;
}

static void
lookupUserGroup(const Context &context, uid_t *uid, struct passwd **userInfo,
	gid_t *gid)
{
	const Json::Value &args = context.args;
	errno = 0;
	*userInfo = getpwnam(args["user"].asCString());
	if (*userInfo == NULL) {
		int e = errno;
		if (looksLikePositiveNumber(args["user"].asString())) {
			fprintf(stderr,
				"Warning: error looking up system user database"
				" entry for user '%s': %s (errno=%d)\n",
				args["user"].asCString(), strerror(e), e);
			*uid = (uid_t) atoi(args["user"].asString());
		} else {
			recordJourneyStepEnd(context, context.step,
				SpawningKit::STEP_ERRORED);
			recordErrorCategory(context.workDir,
				SpawningKit::OPERATING_SYSTEM_ERROR);
			if (e == 0) {
				recordAndPrintErrorSummary(context.workDir,
					"Cannot lookup system user database entry for user '"
					+ args["user"].asString() + "': entry not found",
					true);
			} else {
				recordAndPrintErrorSummary(context.workDir,
					"Cannot lookup system user database entry for user '"
					+ args["user"].asString() + "': " + strerror(e)
					+ " (errno=" + toString(e) + ")",
					true);
			}
			exit(1);
		}
	} else {
		*uid = (*userInfo)->pw_uid;
	}

	errno = 0;
	struct group *groupInfo = getgrnam(args["group"].asCString());
	if (groupInfo == NULL) {
		int e = errno;
		if (looksLikePositiveNumber(args["group"].asString())) {
			fprintf(stderr,
				"Warning: error looking up system group database entry for group '%s':"
				" %s (errno=%d)\n",
				args["group"].asCString(), strerror(e), e);
			*gid = (gid_t) atoi(args["group"].asString());
		} else {
			recordJourneyStepEnd(context, context.step,
				SpawningKit::STEP_ERRORED);
			recordErrorCategory(context.workDir,
				SpawningKit::OPERATING_SYSTEM_ERROR);
			if (e == 0) {
				recordAndPrintErrorSummary(context.workDir,
					"Cannot lookup up system group database entry for group '"
					+ args["group"].asString() + "': entry not found",
					true);
			} else {
				recordAndPrintErrorSummary(context.workDir,
					"Cannot lookup up system group database entry for group '"
					+ args["group"].asString() + "': " + strerror(e)
					+ " (errno=" + toString(e) + ")",
					true);
			}
			exit(1);
		}
	} else {
		*gid = groupInfo->gr_gid;
	}
}

static void
chownNewWorkDirFiles(const Context &context, uid_t uid, gid_t gid) {
	chown((context.workDir + "/response/steps/subprocess_before_first_exec/state").c_str(),
		uid, gid);
	chown((context.workDir + "/response/steps/subprocess_before_first_exec/duration").c_str(),
		uid, gid);
	chown((context.workDir + "/response/steps/subprocess_spawn_env_setupper_before_shell/state").c_str(),
		uid, gid);
	chown((context.workDir + "/response/steps/subprocess_spawn_env_setupper_before_shell/duration").c_str(),
		uid, gid);
	chown((context.workDir + "/envdump/envvars").c_str(),
		uid, gid);
	chown((context.workDir + "/envdump/user_info").c_str(),
		uid, gid);
	chown((context.workDir + "/envdump/ulimits").c_str(),
		uid, gid);
}

static void
finalizeWorkDir(const Context &context, uid_t uid, gid_t gid) {
	SpawningKit::HandshakeWorkDir::finalize(context.workDir, uid, gid);
}

static void
enterLveJail(const Context &context, const struct passwd *userInfo) {
	string lveInitErr;
	adhoc_lve::LibLve &liblve = adhoc_lve::LveInitSignleton::getInstance(&lveInitErr);

	if (liblve.is_error()) {
		if (!lveInitErr.empty()) {
			lveInitErr = ": " + lveInitErr;
		}
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::INTERNAL_ERROR);
		recordAndPrintErrorSummary(context.workDir,
			"Failed to initialize LVE library: " + lveInitErr,
			true);
		exit(1);
	}

	if (!liblve.is_lve_available()) {
		return;
	}

	string jailErr;
	int ret = liblve.jail(userInfo, jailErr);
	if (ret < 0) {
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::INTERNAL_ERROR);
		recordAndPrintErrorSummary(context.workDir,
			"enterLve() failed: " + jailErr,
			true);
		exit(1);
	}
}

static void
switchGroup(const Context &context, uid_t uid, const struct passwd *userInfo, gid_t gid) {
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
				recordJourneyStepEnd(context, context.step,
					SpawningKit::STEP_ERRORED);
				recordErrorCategory(context.workDir,
					SpawningKit::OPERATING_SYSTEM_ERROR);
				recordAndPrintErrorSummary(context.workDir,
					"getgrouplist(" + string(userInfo->pw_name) + ", "
					+ toString(gid) + ") failed: " + strerror(e)
					+ " (errno=" + toString(e) + ")",
					true);
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
					recordJourneyStepEnd(context, context.step,
						SpawningKit::STEP_ERRORED);
					recordErrorCategory(context.workDir,
						SpawningKit::OPERATING_SYSTEM_ERROR);
					recordAndPrintErrorSummary(context.workDir,
						"setgroups(" + toString(ngroups)
						+ ", ...) failed: " + strerror(e) + " (errno="
						+ toString(e) + ")",
						true);
					exit(1);
				}
			}
		#endif

		if (!setgroupsCalled && initgroups(userInfo->pw_name, gid) == -1) {
			int e = errno;
			recordJourneyStepEnd(context, context.step,
				SpawningKit::STEP_ERRORED);
			recordErrorCategory(context.workDir,
				SpawningKit::OPERATING_SYSTEM_ERROR);
			recordAndPrintErrorSummary(context.workDir,
				"initgroups(" + string(userInfo->pw_name)
				+ ", " + toString(gid) + ") failed: " + strerror(e)
				+ " (errno=" + toString(e) + ")",
				true);
			exit(1);
		}
	}

	if (setgid(gid) == -1) {
		int e = errno;
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::OPERATING_SYSTEM_ERROR);
		recordAndPrintErrorSummary(context.workDir,
			"setgid(" + toString(gid) + ") failed: "
			+ strerror(e) + " (errno=" + toString(e) + ")",
			true);
		exit(1);
	}
}

static void
switchUser(const Context &context, uid_t uid, const struct passwd *userInfo) {
	if (setuid(uid) == -1) {
		int e = errno;
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::OPERATING_SYSTEM_ERROR);
		recordAndPrintErrorSummary(context.workDir,
			"setuid(" + toString(uid) + ") failed: " + strerror(e)
			+ " (errno=" + toString(e) + ")",
			true);
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

static string
lookupCurrentUserShell() {
	struct passwd *userInfo = getpwuid(getuid());
	if (userInfo == NULL) {
		int e = errno;
		fprintf(stderr, "Warning: cannot lookup system user database"
			" entry for UID %d: %s (errno=%d)\n",
			(int) getuid(), strerror(e), e);
		return "/bin/sh";
	} else {
		return userInfo->pw_shell;
	}
}

static vector<string>
inferAllParentDirectories(const string &path) {
	vector<string> components, result;

	split(path, '/', components);
	P_ASSERT_EQ(components.front(), "");
	components.erase(components.begin());

	for (unsigned int i = 0; i < components.size(); i++) {
		string path2;
		for (unsigned int j = 0; j <= i; j++) {
			path2.append("/");
			path2.append(components[j]);
		}
		if (path2.empty()) {
			path2 = "/";
		}
		result.push_back(path2);
	}

	P_ASSERT_EQ(result.back(), path);
	return result;
}

static void
setCurrentWorkingDirectory(const Context &context) {
	string appRoot = context.args["app_root"].asString(); // Already absolutized by HandshakePreparer
	vector<string> appRootAndParentDirs = inferAllParentDirectories(appRoot);
	vector<string>::const_iterator it;
	int ret;

	for (it = appRootAndParentDirs.begin(); it != appRootAndParentDirs.end(); it++) {
		struct stat buf;
		ret = stat(it->c_str(), &buf);
		if (ret == -1 && errno == EACCES) {
			char parent[PATH_MAX];
			const char *end = strrchr(it->c_str(), '/');
			memcpy(parent, it->c_str(), end - it->c_str());
			parent[end - it->c_str()] = '\0';

			recordJourneyStepEnd(context, context.step,
				SpawningKit::STEP_ERRORED);
			recordErrorCategory(context.workDir,
				SpawningKit::OPERATING_SYSTEM_ERROR);
			recordAndPrintErrorSummary(context.workDir,
				"Directory '" + string(parent) + "' is inaccessible because of a"
				" filesystem permission error.",
				false);
			recordProblemDescriptionHTML(context.workDir,
				"<p>"
				"The " PROGRAM_NAME " application server tried to start the"
				" web application as user '" + escapeHTML(lookupSystemUsernameByUid(getuid()))
				+ "' and group '" + escapeHTML(lookupSystemGroupnameByGid(getgid()))
				+ "'. During this process, " SHORT_PROGRAM_NAME
				" must be able to access its application root directory '"
				+ escapeHTML(appRoot) + "'. However, the parent directory '"
				+ escapeHTML(parent) + "' has wrong permissions, thereby preventing this"
				" process from accessing its application root directory."
				"</p>");
			recordSolutionDescriptionHTML(context.workDir,
				"<p class=\"sole-solution\">"
				"Please fix the permissions of the directory '" + escapeHTML(appRoot)
				+ "' in such a way that the directory is accessible by user '"
				+ escapeHTML(lookupSystemUsernameByUid(getuid())) + "' and group '"
				+ escapeHTML(lookupSystemGroupnameByGid(getgid())) + "'."
				"</p>");
			exit(1);
		} else if (ret == -1) {
			int e = errno;
			recordJourneyStepEnd(context, context.step,
				SpawningKit::STEP_ERRORED);
			recordErrorCategory(context.workDir,
				SpawningKit::OPERATING_SYSTEM_ERROR);
			recordAndPrintErrorSummary(context.workDir,
				"Unable to stat() directory '" + *it + "': "
				+ strerror(e) + " (errno=" + toString(e) + ")",
				true);
			exit(1);
		}
	}

	ret = chdir(appRoot.c_str());
	if (ret != 0) {
		int e = errno;
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::OPERATING_SYSTEM_ERROR);
		recordAndPrintErrorSummary(context.workDir,
			"Unable to change working directory to '" + appRoot + "': "
			+ strerror(e) + " (errno=" + toString(e) + ")",
			true);
		if (e == EPERM || e == EACCES) {
			recordProblemDescriptionHTML(context.workDir,
				"<p>The " PROGRAM_NAME " application server tried to start the"
				" web application as user " + escapeHTML(lookupSystemUsernameByUid(getuid()))
				+ " and group " + escapeHTML(lookupSystemGroupnameByGid(getgid()))
				+ ", with a working directory of "
				+ escapeHTML(appRoot) + ". However, it encountered a filesystem"
				" permission error while doing this.</p>");
		} else {
			recordProblemDescriptionHTML(context.workDir,
				"<p>The " PROGRAM_NAME " application server tried to start the"
				" web application as user " + escapeHTML(lookupSystemUsernameByUid(getuid()))
				+ " and group " + escapeHTML(lookupSystemGroupnameByGid(getgid()))
				+ ", with a working directory of "
				+ escapeHTML(appRoot) + ". However, it encountered a filesystem"
				" error while doing this.</p>");
		}
		exit(1);
	}

	// The application root may contain one or more symlinks
	// in its path. If the application calls getcwd(), it will
	// get the resolved path.
	//
	// It turns out that there is no such thing as a path without
	// unresolved symlinks. The shell presents a working directory with
	// unresolved symlinks (which it calls the "logical working directory"),
	// but that is an illusion provided by the shell. The shell reports
	// the logical working directory though the PWD environment variable.
	//
	// See also:
	// https://github.com/phusion/passenger/issues/1596#issuecomment-138154045
	// http://git.savannah.gnu.org/cgit/coreutils.git/tree/src/pwd.c
	// http://www.opensource.apple.com/source/shell_cmds/shell_cmds-170/pwd/pwd.c
	setenv("PWD", appRoot.c_str(), 1);
}

static void
setDefaultEnvvars(const Json::Value &args) {
	setenv("PYTHONUNBUFFERED", "1", 1);

	setenv("NODE_PATH", args["node_libdir"].asCString(), 1);

	setenv("RAILS_ENV", args["app_env"].asCString(), 1);
	setenv("RACK_ENV", args["app_env"].asCString(), 1);
	setenv("WSGI_ENV", args["app_env"].asCString(), 1);
	setenv("NODE_ENV", args["app_env"].asCString(), 1);
	setenv("PASSENGER_APP_ENV", args["app_env"].asCString(), 1);

	if (args.isMember("expected_start_port")) {
		setenv("PORT", toString(args["expected_start_port"].asInt()).c_str(), 1);
	}

	if (args["base_uri"].asString() != "/") {
		setenv("RAILS_RELATIVE_URL_ROOT", args["base_uri"].asCString(), 1);
		setenv("RACK_BASE_URI", args["base_uri"].asCString(), 1);
		setenv("PASSENGER_BASE_URI", args["base_uri"].asCString(), 1);
	} else {
		unsetenv("RAILS_RELATIVE_URL_ROOT");
		unsetenv("RACK_BASE_URI");
		unsetenv("PASSENGER_BASE_URI");
	}
}

static void
setGivenEnvVars(const Json::Value &args) {
	const Json::Value &envvars = args["environment_variables"];
	Json::Value::const_iterator it, end = envvars.end();

	for (it = envvars.begin(); it != end; it++) {
		string key = it.name();
		setenv(key.c_str(), it->asCString(), 1);
	}
}

static bool
shouldLoadShellEnvvars(const Json::Value &args, const string &shell) {
	// Note: `shell` could be empty:
	// https://github.com/phusion/passenger/issues/2078
	if (args["load_shell_envvars"].asBool()) {
		string shellName = extractBaseName(shell);
		bool result = shellName == "bash" || shellName == "zsh" || shellName == "ksh";
		#if defined(__linux__) || defined(__APPLE__)
			// On Linux, /bin/sh is usually either bash or dash, which
			// supports -l.
			// On macOS, it is not clear what /bin/sh is but
			// it supports -l.
			// This cannot be said of other platforms: for example on
			// FreeBSD, /bin/sh does not support -l.
			result = result || shellName == "sh";
		#endif
		P_DEBUG("shellName = '" << shellName << "' detected as supporting '-l': " << (result ? "true" : "false"));
		return result;
	} else {
		return false;
	}
}

static string
commandArgsToString(const vector<const char *> &commandArgs) {
	vector<const char *>::const_iterator it;
	string result;

	for (it = commandArgs.begin(); it != commandArgs.end(); it++) {
		if (*it != NULL) {
			result.append(*it);
			result.append(1, ' ');
		}
	}

	return strip(result);
}

static bool
executedThroughShell(const Context &context) {
	return fileExists(context.workDir + "/execute_through_os_shell");
}

static void
execNextCommand(const Context &context, const string &shell)
{
	vector<const char *> commandArgs;
	SpawningKit::JourneyStep nextJourneyStep;
	string binShPath, binShParam;

	// Note: do not try to set a process title in this function by messing with argv[0].
	// https://code.google.com/p/phusion-passenger/issues/detail?id=855

	if (context.mode == BEFORE_MODE) {
		// Note: `shell` could be empty:
		// https://github.com/phusion/passenger/issues/2078
		if (shouldLoadShellEnvvars(context.args, shell)) {
			nextJourneyStep = SpawningKit::SUBPROCESS_OS_SHELL;
			commandArgs.push_back(shell.c_str());
			if (LoggingKit::getLevel() >= LoggingKit::DEBUG3) {
				commandArgs.push_back("-x");
			}
			commandArgs.push_back("-lc");
			commandArgs.push_back("exec \"$@\"");
			commandArgs.push_back("SpawnEnvSetupperShell");

			// Will be used by 'spawn-env-setupper --after' to determine
			// whether it should set the SUBPROCESS_OS_SHELL step to the
			// PERFORMED state.
			tryWriteFile(context.workDir + "/execute_through_os_shell", "");
		} else {
			nextJourneyStep = SpawningKit::SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL;
		}
		commandArgs.push_back(context.args["passenger_agent_path"].asCString());
		commandArgs.push_back("spawn-env-setupper");
		commandArgs.push_back(context.workDir.c_str());
		commandArgs.push_back("--after");
	} else {
		if (context.args["starts_using_wrapper"].asBool()) {
			nextJourneyStep = SpawningKit::SUBPROCESS_EXEC_WRAPPER;
		} else {
			nextJourneyStep = SpawningKit::SUBPROCESS_APP_LOAD_OR_EXEC;
		}
		if (context.args.isMember("_bin_sh_path")) {
			// Used in unit tests
			binShPath = context.args["_bin_sh_path"].asString();
		} else {
			binShPath = "/bin/sh";
		}
		binShParam = "exec " + context.args["start_command"].asString();
		commandArgs.push_back(binShPath.c_str());
		commandArgs.push_back("-c");
		commandArgs.push_back(binShParam.c_str());
	}
	commandArgs.push_back(NULL);

	recordJourneyStepEnd(context, context.step,
		SpawningKit::STEP_PERFORMED);
	recordJourneyStepBegin(context, nextJourneyStep,
		SpawningKit::STEP_IN_PROGRESS);

	execvp(commandArgs[0], (char * const *) &commandArgs[0]);

	int e = errno;
	recordJourneyStepEnd(context, nextJourneyStep,
		SpawningKit::STEP_ERRORED);
	recordErrorCategory(context.workDir, SpawningKit::OPERATING_SYSTEM_ERROR);
	recordAndPrintErrorSummary(context.workDir,
		"Unable to execute command '" + commandArgsToString(commandArgs)
		+ "': " + strerror(e) + " (errno=" + toString(e) + ")",
		true);
	exit(1);
}

int
spawnEnvSetupperMain(int argc, char *argv[]) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (argc != 4) {
		fprintf(stderr, "Usage: PassengerAgent spawn-env-setupper <workdir> <--before|--after>\n");
		exit(1);
	}

	oxt::initialize();
	oxt::setup_syscall_interruption_support();
	LoggingKit::initialize();
	SystemTime::initialize();

	Context context;
	context.workDir = argv[2];
	context.mode =
		(strcmp(argv[3], "--before") == 0)
		? BEFORE_MODE
		: AFTER_MODE;
	context.step =
		(context.mode == BEFORE_MODE)
		? SpawningKit::SUBPROCESS_SPAWN_ENV_SETUPPER_BEFORE_SHELL
		: SpawningKit::SUBPROCESS_SPAWN_ENV_SETUPPER_AFTER_SHELL;

	setenv("IN_PASSENGER", "1", 1);
	setenv("PASSENGER_SPAWN_WORK_DIR", context.workDir.c_str(), 1);
	if (context.mode == BEFORE_MODE) {
		recordJourneyStepEnd(context, SpawningKit::SUBPROCESS_BEFORE_FIRST_EXEC,
			SpawningKit::STEP_PERFORMED);
	}
	recordJourneyStepBegin(context, context.step,
		SpawningKit::STEP_IN_PROGRESS);

	try {
		context.args = readArgsJson(context.workDir);
		bool shouldTrySwitchUser = canSwitchUser(context.args);
		string shell;

		initializeLogLevel(context.args);
		dumpAllEnvironmentInfo(context.workDir);

		if (context.mode == BEFORE_MODE) {
			struct passwd *userInfo = NULL;
			uid_t uid;
			gid_t gid;

			setDefaultEnvvars(context.args);
			dumpEnvvars(context.workDir);

			if (shouldTrySwitchUser) {
				lookupUserGroup(context, &uid, &userInfo, &gid);
				shell = userInfo->pw_shell;
			} else {
				uid = geteuid();
				gid = getegid();
				shell = lookupCurrentUserShell();
			}
			if (setUlimits(context.args)) {
				dumpUlimits(context.workDir);
			}
			if (shouldTrySwitchUser) {
				chownNewWorkDirFiles(context, uid, gid);
				finalizeWorkDir(context, uid, gid);

				enterLveJail(context, userInfo);
				switchGroup(context, uid, userInfo, gid);
				dumpUserInfo(context.workDir);

				switchUser(context, uid, userInfo);
				dumpEnvvars(context.workDir);
				dumpUserInfo(context.workDir);
			} else {
				finalizeWorkDir(context, uid, gid);
			}
		} else if (executedThroughShell(context)) {
			recordJourneyStepEnd(context, SpawningKit::SUBPROCESS_OS_SHELL,
				SpawningKit::STEP_PERFORMED);
		} else {
			recordJourneyStepEnd(context, SpawningKit::SUBPROCESS_OS_SHELL,
				SpawningKit::STEP_NOT_STARTED);
		}

		setCurrentWorkingDirectory(context);
		dumpEnvvars(context.workDir);

		if (context.mode == AFTER_MODE) {
			setDefaultEnvvars(context.args);
			setGivenEnvVars(context.args);
			dumpEnvvars(context.workDir);
		}

		execNextCommand(context, shell);
	} catch (const oxt::tracable_exception &e) {
		fprintf(stderr, "Error: %s\n%s\n",
			e.what(), e.backtrace().c_str());
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::inferErrorCategoryFromAnotherException(
				e, context.step));
		recordErrorSummary(context.workDir, e.what(), true);
		return 1;
	} catch (const std::exception &e) {
		fprintf(stderr, "Error: %s\n", e.what());
		recordJourneyStepEnd(context, context.step,
			SpawningKit::STEP_ERRORED);
		recordErrorCategory(context.workDir,
			SpawningKit::inferErrorCategoryFromAnotherException(
				e, context.step));
		recordErrorSummary(context.workDir, e.what(), true);
		return 1;
	}

	// Should never be reached
	recordJourneyStepEnd(context, context.step,
		SpawningKit::STEP_ERRORED);
	recordAndPrintErrorSummary(context.workDir,
		"*** BUG IN SpawnEnvSetupper ***: end of main() reached",
		true);
	return 1;
}

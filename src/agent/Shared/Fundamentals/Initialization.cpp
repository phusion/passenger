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

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#include <Shared/Fundamentals/Initialization.h>
#include <Shared/Fundamentals/AbortHandler.h>
#include <Shared/Fundamentals/Utils.h>

#include <oxt/initialize.hpp>
#include <oxt/backtrace.hpp>
#include <string>
#include <vector>

#include <sys/types.h>
#include <stdlib.h> // for srandom()
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <Exceptions.h>
#include <ResourceLocator.h>
#include <LoggingKit/LoggingKit.h>
#include <LoggingKit/Context.h>
#include <Utils/StrIntUtils.h>
#include <Utils/MessageIO.h>
#include <Utils/SystemTime.h>


namespace Passenger {
namespace Agent {
namespace Fundamentals {

using namespace std;


Context *context = NULL;


bool
feedbackFdAvailable() {
	return context->feedbackFdAvailable;
}

static int
lookupErrno(const char *name) {
	struct Entry {
		int errorCode;
		const char * const name;
	};
	static const Entry entries[] = {
		{ EPERM, "EPERM" },
		{ ENOENT, "ENOENT" },
		{ ESRCH, "ESRCH" },
		{ EINTR, "EINTR" },
		{ EBADF, "EBADF" },
		{ ENOMEM, "ENOMEM" },
		{ EACCES, "EACCES" },
		{ EBUSY, "EBUSY" },
		{ EEXIST, "EEXIST" },
		{ ENOTDIR, "ENOTDIR" },
		{ EISDIR, "EISDIR" },
		{ EINVAL, "EINVAL" },
		{ ENFILE, "ENFILE" },
		{ EMFILE, "EMFILE" },
		{ ENOTTY, "ENOTTY" },
		{ ETXTBSY, "ETXTBSY" },
		{ ENOSPC, "ENOSPC" },
		{ ESPIPE, "ESPIPE" },
		{ EMLINK, "EMLINK" },
		{ EPIPE, "EPIPE" },
		{ EAGAIN, "EAGAIN" },
		{ EWOULDBLOCK, "EWOULDBLOCK" },
		{ EINPROGRESS, "EINPROGRESS" },
		{ EADDRINUSE, "EADDRINUSE" },
		{ EADDRNOTAVAIL, "EADDRNOTAVAIL" },
		{ ENETUNREACH, "ENETUNREACH" },
		{ ECONNABORTED, "ECONNABORTED" },
		{ ECONNRESET, "ECONNRESET" },
		{ EISCONN, "EISCONN" },
		{ ENOTCONN, "ENOTCONN" },
		{ ETIMEDOUT, "ETIMEDOUT" },
		{ ECONNREFUSED, "ECONNREFUSED" },
		{ EHOSTDOWN, "EHOSTDOWN" },
		{ EHOSTUNREACH, "EHOSTUNREACH" },
		#ifdef EIO
			{ EIO, "EIO" },
		#endif
		#ifdef ENXIO
			{ ENXIO, "ENXIO" },
		#endif
		#ifdef E2BIG
			{ E2BIG, "E2BIG" },
		#endif
		#ifdef ENOEXEC
			{ ENOEXEC, "ENOEXEC" },
		#endif
		#ifdef ECHILD
			{ ECHILD, "ECHILD" },
		#endif
		#ifdef EDEADLK
			{ EDEADLK, "EDEADLK" },
		#endif
		#ifdef EFAULT
			{ EFAULT, "EFAULT" },
		#endif
		#ifdef ENOTBLK
			{ ENOTBLK, "ENOTBLK" },
		#endif
		#ifdef EXDEV
			{ EXDEV, "EXDEV" },
		#endif
		#ifdef ENODEV
			{ ENODEV, "ENODEV" },
		#endif
		#ifdef EFBIG
			{ EFBIG, "EFBIG" },
		#endif
		#ifdef EROFS
			{ EROFS, "EROFS" },
		#endif
		#ifdef EDOM
			{ EDOM, "EDOM" },
		#endif
		#ifdef ERANGE
			{ ERANGE, "ERANGE" },
		#endif
		#ifdef EALREADY
			{ EALREADY, "EALREADY" },
		#endif
		#ifdef ENOTSOCK
			{ ENOTSOCK, "ENOTSOCK" },
		#endif
		#ifdef EDESTADDRREQ
			{ EDESTADDRREQ, "EDESTADDRREQ" },
		#endif
		#ifdef EMSGSIZE
			{ EMSGSIZE, "EMSGSIZE" },
		#endif
		#ifdef EPROTOTYPE
			{ EPROTOTYPE, "EPROTOTYPE" },
		#endif
		#ifdef ENOPROTOOPT
			{ ENOPROTOOPT, "ENOPROTOOPT" },
		#endif
		#ifdef EPROTONOSUPPORT
			{ EPROTONOSUPPORT, "EPROTONOSUPPORT" },
		#endif
		#ifdef ESOCKTNOSUPPORT
			{ ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT" },
		#endif
		#ifdef ENOTSUP
			{ ENOTSUP, "ENOTSUP" },
		#endif
		#ifdef EOPNOTSUPP
			{ EOPNOTSUPP, "EOPNOTSUPP" },
		#endif
		#ifdef EPFNOSUPPORT
			{ EPFNOSUPPORT, "EPFNOSUPPORT" },
		#endif
		#ifdef EAFNOSUPPORT
			{ EAFNOSUPPORT, "EAFNOSUPPORT" },
		#endif
		#ifdef ENETDOWN
			{ ENETDOWN, "ENETDOWN" },
		#endif
		#ifdef ENETRESET
			{ ENETRESET, "ENETRESET" },
		#endif
		#ifdef ENOBUFS
			{ ENOBUFS, "ENOBUFS" },
		#endif
		#ifdef ESHUTDOWN
			{ ESHUTDOWN, "ESHUTDOWN" },
		#endif
		#ifdef ETOOMANYREFS
			{ ETOOMANYREFS, "ETOOMANYREFS" },
		#endif
		#ifdef ELOOP
			{ ELOOP, "ELOOP" },
		#endif
		#ifdef ENAMETOOLONG
			{ ENAMETOOLONG, "ENAMETOOLONG" },
		#endif
		#ifdef ENOTEMPTY
			{ ENOTEMPTY, "ENOTEMPTY" },
		#endif
		#ifdef EPROCLIM
			{ EPROCLIM, "EPROCLIM" },
		#endif
		#ifdef EUSERS
			{ EUSERS, "EUSERS" },
		#endif
		#ifdef EDQUOT
			{ EDQUOT, "EDQUOT" },
		#endif
		#ifdef ESTALE
			{ ESTALE, "ESTALE" },
		#endif
		#ifdef EREMOTE
			{ EREMOTE, "EREMOTE" },
		#endif
		#ifdef EBADRPC
			{ EBADRPC, "EBADRPC" },
		#endif
		#ifdef ERPCMISMATCH
			{ ERPCMISMATCH, "ERPCMISMATCH" },
		#endif
		#ifdef EPROGUNAVAIL
			{ EPROGUNAVAIL, "EPROGUNAVAIL" },
		#endif
		#ifdef EPROGMISMATCH
			{ EPROGMISMATCH, "EPROGMISMATCH" },
		#endif
		#ifdef EPROCUNAVAIL
			{ EPROCUNAVAIL, "EPROCUNAVAIL" },
		#endif
		#ifdef ENOLCK
			{ ENOLCK, "ENOLCK" },
		#endif
		#ifdef ENOSYS
			{ ENOSYS, "ENOSYS" },
		#endif
		#ifdef EFTYPE
			{ EFTYPE, "EFTYPE" },
		#endif
		#ifdef EAUTH
			{ EAUTH, "EAUTH" },
		#endif
		#ifdef ENEEDAUTH
			{ ENEEDAUTH, "ENEEDAUTH" },
		#endif
		#ifdef EPWROFF
			{ EPWROFF, "EPWROFF" },
		#endif
		#ifdef EDEVERR
			{ EDEVERR, "EDEVERR" },
		#endif
		#ifdef EOVERFLOW
			{ EOVERFLOW, "EOVERFLOW" },
		#endif
		#ifdef EBADEXEC
			{ EBADEXEC, "EBADEXEC" },
		#endif
		#ifdef EBADARCH
			{ EBADARCH, "EBADARCH" },
		#endif
		#ifdef ESHLIBVERS
			{ ESHLIBVERS, "ESHLIBVERS" },
		#endif
		#ifdef EBADMACHO
			{ EBADMACHO, "EBADMACHO" },
		#endif
		#ifdef ECANCELED
			{ ECANCELED, "ECANCELED" },
		#endif
		#ifdef EIDRM
			{ EIDRM, "EIDRM" },
		#endif
		#ifdef ENOMSG
			{ ENOMSG, "ENOMSG" },
		#endif
		#ifdef EILSEQ
			{ EILSEQ, "EILSEQ" },
		#endif
		#ifdef ENOATTR
			{ ENOATTR, "ENOATTR" },
		#endif
		#ifdef EBADMSG
			{ EBADMSG, "EBADMSG" },
		#endif
		#ifdef EMULTIHOP
			{ EMULTIHOP, "EMULTIHOP" },
		#endif
		#ifdef ENODATA
			{ ENODATA, "ENODATA" },
		#endif
		#ifdef ENOLINK
			{ ENOLINK, "ENOLINK" },
		#endif
		#ifdef ENOSR
			{ ENOSR, "ENOSR" },
		#endif
		#ifdef ENOSTR
			{ ENOSTR, "ENOSTR" },
		#endif
		#ifdef EPROTO
			{ EPROTO, "EPROTO" },
		#endif
		#ifdef ETIME
			{ ETIME, "ETIME" },
		#endif
		#ifdef EOPNOTSUPP
			{ EOPNOTSUPP, "EOPNOTSUPP" },
		#endif
		#ifdef ENOPOLICY
			{ ENOPOLICY, "ENOPOLICY" },
		#endif
		#ifdef ENOTRECOVERABLE
			{ ENOTRECOVERABLE, "ENOTRECOVERABLE" },
		#endif
		#ifdef EOWNERDEAD
			{ EOWNERDEAD, "EOWNERDEAD" },
		#endif
	};

	for (unsigned int i = 0; i < sizeof(entries) / sizeof(Entry); i++) {
		if (strcmp(entries[i].name, name) == 0) {
			return entries[i].errorCode;
		}
	}
	return -1;
}

static void
initializeSyscallFailureSimulation(const char *processName) {
	// Format:
	// PassengerAgent watchdog=EMFILE:0.1,ECONNREFUSED:0.25;PassengerAgent core=ESPIPE=0.4
	const char *spec = getenv("PASSENGER_SIMULATE_SYSCALL_FAILURES");
	string prefix = string(processName) + "=";
	vector<string> components;
	unsigned int i;

	// Lookup this process in the specification string.
	split(spec, ';', components);
	for (i = 0; i < components.size(); i++) {
		if (startsWith(components[i], prefix)) {
			// Found!
			string value = components[i].substr(prefix.size());
			split(value, ',', components);
			vector<string> keyAndValue;
			vector<ErrorChance> chances;

			// Process each errorCode:chance pair.
			for (i = 0; i < components.size(); i++) {
				split(components[i], ':', keyAndValue);
				if (keyAndValue.size() != 2) {
					fprintf(stderr, "%s: invalid syntax in PASSENGER_SIMULATE_SYSCALL_FAILURES: '%s'\n",
						processName, components[i].c_str());
					continue;
				}

				int e = lookupErrno(keyAndValue[0].c_str());
				if (e == -1) {
					fprintf(stderr, "%s: invalid error code in PASSENGER_SIMULATE_SYSCALL_FAILURES: '%s'\n",
						processName, components[i].c_str());
					continue;
				}

				ErrorChance chance;
				chance.chance = atof(keyAndValue[1].c_str());
				if (chance.chance < 0 || chance.chance > 1) {
					fprintf(stderr, "%s: invalid chance PASSENGER_SIMULATE_SYSCALL_FAILURES: '%s' - chance must be between 0 and 1\n",
						processName, components[i].c_str());
					continue;
				}
				chance.errorCode = e;
				chances.push_back(chance);
			}

			// Install the chances.
			setup_random_failure_simulation(&chances[0], chances.size());
			return;
		}
	}
}

static bool
isBlank(const char *str) {
	while (*str != '\0') {
		if (*str != ' ') {
			return false;
		}
		str++;
	}
	return true;
}

static bool
extraArgumentsPassed(int argc, char *argv[], int argStartIndex) {
	assert(argc >= argStartIndex);
	return argc > argStartIndex + 1
		// Allow the Watchdog to pass an all-whitespace argument. This
		// argument provides the memory space for us to change the process title.
		|| (argc == argStartIndex + 1 && !isBlank(argv[argStartIndex]));
}

static void
maybeInitializeAbortHandler() {
	if (!getEnvBool("PASSENGER_ABORT_HANDLER", true)) {
		return;
	}

	AbortHandlerConfig *config = &context->abortHandlerConfig;

	config->origArgv = context->origArgv;
	config->randomSeed = context->randomSeed;
	config->dumpWithCrashWatch = getEnvBool("PASSENGER_DUMP_WITH_CRASH_WATCH", true);
	config->beep = getEnvBool("PASSENGER_BEEP_ON_ABORT");
	config->stopProcess = getEnvBool("PASSENGER_STOP_ON_ABORT");

	installAbortHandler(config);
}

static void
maybeInitializeSyscallFailureSimulation(const char *processName) {
	if (getEnvBool("PASSENGER_SIMULATE_SYSCALL_FAILURES")) {
		initializeSyscallFailureSimulation(processName);
	}
}

static void
initializeLoggingKit(const char *processName, VariantMap &options) {
	Json::Value config(Json::objectValue);

	options.setDefaultInt("log_level", DEFAULT_LOG_LEVEL);
	config["level"] = options.get("log_level");

	if (options.has("log_file")) {
		config["target"]["path"] = options.get("log_file");
	} else if (options.has("debug_log_file")) {
		config["target"]["path"] = options.get("debug_log_file");
	}
	if (options.getBool("log_file_is_stderr", false, false)) {
		config["target"]["stderr"] = true;
	}

	if (options.has("file_descriptor_log_file")) {
		config["file_descriptor_log_target"] =
			options.get("file_descriptor_log_file");
	}

	LoggingKit::initialize(config);

	if (options.has("file_descriptor_log_file")) {
		// This information helps ./dev/parse_file_descriptor_log.
		FastStringStream<> stream;
		LoggingKit::_prepareLogEntry(stream, LoggingKit::CRIT, __FILE__, __LINE__);
		stream << "Starting agent: " << processName << "\n";
		_writeFileDescriptorLogEntry(LoggingKit::context->getConfigRealization(),
			stream.data(), stream.size());

		config = LoggingKit::context->getConfig().inspect();
		P_LOG_FILE_DESCRIPTOR_OPEN4(
			LoggingKit::context->getConfigRealization()->fileDescriptorLogTargetFd,
			__FILE__, __LINE__,
			"file descriptor log file "
			<< config["file_descriptor_log_target"]["effective_value"]["path"].asString());
	} else {
		// This information helps ./dev/parse_file_descriptor_log.
		P_DEBUG("Starting agent: " << processName);
	}

	if (getEnvBool("PASSENGER_USE_FEEDBACK_FD")) {
		P_LOG_FILE_DESCRIPTOR_OPEN2(FEEDBACK_FD, "feedback FD");
	}
	if (abortHandlerInstalled()) {
		abortHandlerLogFds();
	}
}

static void
storeArgvCopy(int argc, char *argv[]) {
	// Make a copy of the arguments before changing process title.
	context->origArgc = argc;
	context->origArgv = (char **) malloc(argc * sizeof(char *));
	for (int i = 0; i < argc; i++) {
		context->origArgv[i] = strdup(argv[i]);
	}
}

static void
changeProcessTitle(int argc, char **argv[], const char *processName) {
	size_t totalArgLen = strlen((*argv)[0]);
	for (int i = 1; i < argc; i++) {
		size_t len = strlen((*argv)[i]);
		totalArgLen += len + 1;
		memset((*argv)[i], '\0', len);
	}
	strncpy((*argv)[0], processName, totalArgLen);
	*argv = context->origArgv;
}

VariantMap
initializeAgent(int argc, char **argv[], const char *processName,
	OptionParserFunc optionParser, PreinitializationFunc preinit,
	int argStartIndex)
{
	VariantMap options;
	const char *seedStr;

	context = new Context();
	memset(context, 0, sizeof(Context));

	seedStr = getEnvString("PASSENGER_RANDOM_SEED");
	if (seedStr == NULL) {
		context->randomSeed = (unsigned int) time(NULL);
	} else {
		context->randomSeed = (unsigned int) atoll(seedStr);
	}
	srand(context->randomSeed);
	srandom(context->randomSeed);

	ignoreSigpipe();
	maybeInitializeAbortHandler();
	oxt::initialize();
	setup_syscall_interruption_support();
	maybeInitializeSyscallFailureSimulation(processName);
	SystemTime::initialize();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	TRACE_POINT();
	try {
		if (getEnvBool("PASSENGER_USE_FEEDBACK_FD")) {
			if (extraArgumentsPassed(argc, *argv, argStartIndex)) {
				fprintf(stderr, "No arguments may be passed when using the feedback FD.\n");
				exit(1);
			}
			context->feedbackFdAvailable = true;
			options.readFrom(FEEDBACK_FD);
		} else if (optionParser != NULL) {
			optionParser(argc, (const char **) *argv, options);
		} else {
			options.readFrom((const char **) *argv + argStartIndex,
				argc - argStartIndex);
		}

		if (options.has("passenger_root")) {
			context->resourceLocator = new ResourceLocator(options.get("passenger_root", true));
			if (abortHandlerInstalled()) {
				context->abortHandlerConfig.ruby = strdup(options.get("default_ruby", false, DEFAULT_RUBY).c_str());
				context->abortHandlerConfig.resourceLocator = context->resourceLocator;
				abortHandlerConfigChanged();
			}
		}

		if (preinit != NULL) {
			preinit(options);
		}

		initializeLoggingKit(processName, options);
	} catch (const tracable_exception &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
		exit(1);
	}

	storeArgvCopy(argc, *argv);
	changeProcessTitle(argc, argv, processName);

	P_DEBUG("Random seed: " << context->randomSeed);

	return options;
}

void
shutdownAgent(VariantMap *agentOptions) {
	delete agentOptions;
	LoggingKit::shutdown();
	oxt::shutdown();
	if (abortHandlerInstalled()) {
		shutdownAbortHandler();
		free(context->abortHandlerConfig.ruby);
	}
	for (int i = 0; i < context->origArgc; i++) {
		free(context->origArgv[i]);
	}
	free(context->origArgv);
	delete context->resourceLocator;
	delete context;
	context = NULL;
}

void
restoreOomScore(VariantMap *agentOptions) {
	string score = agentOptions->get("original_oom_score", false);
	bool isLegacy;
	int ret = tryRestoreOomScore(score, isLegacy);
	if (ret != 0) {
		P_WARN("Unable to set OOM score to " << score << " (legacy: " << isLegacy
			<< ") due to error: " << strerror(ret)
			<< " (process will remain at inherited OOM score)");
	}
}


} // namespace Fundamentals
} // namespace Agent
} // namespace Passenger

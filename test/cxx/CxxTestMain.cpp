#include "TestSupport.h"
#include "../tut/tut_reporter.h"
#include "../support/valgrind.h"
#include <oxt/initialize.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <MultiLibeio.cpp>
#include <Utils.h>
#include <Utils/IOUtils.h>
#include <Utils/json.h>

using namespace std;

namespace tut {
	test_runner_singleton runner;
}

typedef tut::groupnames::const_iterator groupnames_iterator;

/** All available groups. */
static tut::groupnames allGroups;

/** Whether the user wants to run all test groups, or only the specified test groups. */
static enum { RUN_ALL_GROUPS, RUN_SPECIFIED_GROUPS } runMode = RUN_ALL_GROUPS;

/** The test groups the user wants to run. Only meaningful if runMode == RUN_SPECIFIED_GROUPS. */
static tut::groupnames groupsToRun;


static void
usage(int exitCode) {
	printf("Usage: ./Apache2ModuleTests [options]\n");
	printf("Runs the unit tests for the Apache 2 module.\n\n");
	printf("Options:\n");
	printf("  -g GROUP_NAME   Instead of running all unit tests, only run the test group\n");
	printf("                  named GROUP_NAME. You can specify -g multiple times, which\n");
	printf("                  will result in only the specified test groups being run.\n\n");
	printf("                  Available test groups:\n\n");
	for (groupnames_iterator it = allGroups.begin(); it != allGroups.end(); it++) {
		printf("                    %s\n", it->c_str());
	}
	printf("\n");
	printf("  -h              Print this usage information.\n");
	exit(exitCode);
}

static bool
groupExists(const string &name) {
	for (groupnames_iterator it = allGroups.begin(); it != allGroups.end(); it++) {
		if (name == *it) {
			return true;
		}
	}
	return false;
}

static void
parseOptions(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			usage(0);
		} else if (strcmp(argv[i], "-g") == 0) {
			if (argv[i + 1] == NULL) {
				fprintf(stderr, "*** ERROR: A -g option must be followed by a test group name.\n");
				exit(1);
			} else if (!groupExists(argv[i + 1])) {
				fprintf(stderr,
					"*** ERROR: Invalid test group '%s'. Available test groups are:\n\n",
					argv[i + 1]);
				for (groupnames_iterator it = allGroups.begin(); it != allGroups.end(); it++) {
					printf("%s\n", it->c_str());
				}
				exit(1);
			} else {
				runMode = RUN_SPECIFIED_GROUPS;
				groupsToRun.push_back(argv[i + 1]);
				i++;
			}
		} else {
			fprintf(stderr, "*** ERROR: Unknown option: %s\n", argv[i]);
			fprintf(stderr, "Please pass -h for a list of valid options.\n");
			exit(1);
		}
	}
}

static int
doNothing(eio_req *req) {
	return 0;
}

static void
loadConfigFile() {
	Json::Reader reader;
	if (!reader.parse(readAll("config.json"), testConfig)) {
		fprintf(stderr, "Cannot parse config.json: %s\n",
			reader.getFormattedErrorMessages().c_str());
		exit(1);
	}
}

static void
abortHandler(int signo, siginfo_t *info, void *ctx) {
	// Stop itself so that we can attach it to gdb.
	raise(SIGSTOP);
	// Run default signal handler.
	raise(signo);
}

static void
installAbortHandler() {
	const char *stopOnAbort = getenv("STOP_ON_ABORT");
	if (stopOnAbort != NULL && *stopOnAbort != '\0' && *stopOnAbort != '0') {
		struct sigaction action;
		action.sa_sigaction = abortHandler;
		action.sa_flags = SA_RESETHAND | SA_SIGINFO;
		sigemptyset(&action.sa_mask);
		sigaction(SIGABRT, &action, NULL);
		sigaction(SIGSEGV, &action, NULL);
		sigaction(SIGBUS, &action, NULL);
		sigaction(SIGFPE, &action, NULL);
	}
}

int
main(int argc, char *argv[]) {
	signal(SIGPIPE, SIG_IGN);
	setenv("RAILS_ENV", "production", 1);
	setenv("TESTING_PASSENGER", "1", 1);
	setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
	unsetenv("PASSENGER_TMPDIR");
	unsetenv("PASSENGER_TEMP_DIR");
	oxt::initialize();
	oxt::setup_syscall_interruption_support();
    
	tut::reporter reporter;
	tut::runner.get().set_callback(&reporter);
	allGroups = tut::runner.get().list_groups();
	parseOptions(argc, argv);
	
	char path[PATH_MAX + 1];
	getcwd(path, PATH_MAX);
	resourceLocator = new ResourceLocator(extractDirName(path));

	Passenger::MultiLibeio::init();
	eio_set_idle_timeout(9999); // Never timeout.
	eio_set_min_parallel(1);
	eio_set_max_parallel(1);
	if (RUNNING_ON_VALGRIND) {
		// Start an EIO thread to warm up Valgrind.
		eio_nop(0, doNothing, NULL);
	}

	loadConfigFile();
	installAbortHandler();
	
	bool all_ok = true;
	if (runMode == RUN_ALL_GROUPS) {
		tut::runner.get().run_tests();
		all_ok = reporter.all_ok();
	} else {
		all_ok = true;
		for (groupnames_iterator it = groupsToRun.begin(); it != groupsToRun.end(); it++) {
			tut::runner.get().run_tests(*it);
			all_ok = all_ok && reporter.all_ok();
		}
	}
	Passenger::MultiLibeio::shutdown();
	if (all_ok) {
		return 0;
	} else {
		return 1;
	}
}

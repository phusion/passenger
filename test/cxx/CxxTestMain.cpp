#include <TestSupport.h>
#include "../tut/tut_reporter.h"
#include <oxt/initialize.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <map>
#include <vector>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <Shared/Fundamentals/Initialization.h>
#include <Shared/Fundamentals/AbortHandler.h>
#include <Shared/Fundamentals/Utils.h>
#include <ConfigKit/ConfigKit.h>
#include <LoggingKit/LoggingKit.h>
#include <FileTools/PathManip.h>
#include <Utils.h>
#include <Utils/SystemTime.h>
#include <Utils/StrIntUtils.h>
#include <jsoncpp/json.h>

using namespace std;
using namespace Passenger;
using namespace TestSupport;

namespace tut {
	test_runner_singleton runner;
}

typedef tut::groupnames::const_iterator groupnames_iterator;

/** All available groups. */
static tut::groupnames allGroups;

/** Whether the user wants to run all test groups, or only the specified test groups. */
enum RunMode { RUN_ALL_GROUPS, RUN_SPECIFIED_GROUPS };
static RunMode runMode = RUN_ALL_GROUPS;

/** The test groups and test numbers that the user wants to run.
 * Only meaningful if runMode == RUN_SPECIFIED_GROUPS.
 */
static map< string, vector<int> > groupsToRun;


static ConfigKit::Schema *
createSchema() {
	using namespace ConfigKit;

	ConfigKit::Schema *schema = new ConfigKit::Schema();
	schema->add("passenger_root", STRING_TYPE, REQUIRED);
	schema->finalize();

	return schema;
}

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
parseGroupSpec(const char *spec, string &groupName, vector<int> &testNumbers) {
	testNumbers.clear();
	if (*spec == '\0') {
		groupName = "";
		return;
	}

	vector<string> components;
	split(spec, ':', components);
	groupName = components[0];
	if (components.size() > 1) {
		string testNumbersSpec = components[1];
		components.clear();
		split(testNumbersSpec, ',', components);
		vector<string>::const_iterator it;
		for (it = components.begin(); it != components.end(); it++) {
			testNumbers.push_back(atoi(*it));
		}
	}
}

static void
parseOptions(int argc, const char *argv[], ConfigKit::Store &config) {
	Json::Value updates;
	char path[PATH_MAX + 1];
	getcwd(path, PATH_MAX);
	updates["passenger_root"] = extractDirName(path);

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			usage(0);
		} else if (strcmp(argv[i], "-g") == 0) {
			if (argv[i + 1] == NULL) {
				fprintf(stderr, "*** ERROR: A -g option must be followed by a test group name.\n");
				exit(1);
			}

			string groupName;
			vector<int> testNumbers;
			parseGroupSpec(argv[i + 1], groupName, testNumbers);

			if (!groupExists(groupName)) {
				fprintf(stderr,
					"*** ERROR: Invalid test group '%s'. Available test groups are:\n\n",
					argv[i + 1]);
				for (groupnames_iterator it = allGroups.begin(); it != allGroups.end(); it++) {
					printf("%s\n", it->c_str());
				}
				exit(1);
			} else {
				runMode = RUN_SPECIFIED_GROUPS;
				groupsToRun[groupName] = testNumbers;
				i++;
			}
		} else {
			fprintf(stderr, "*** ERROR: Unknown option: %s\n", argv[i]);
			fprintf(stderr, "Please pass -h for a list of valid options.\n");
			exit(1);
		}
	}

	vector<ConfigKit::Error> errors;
	if (!config.update(updates, errors)) {
		P_BUG("Unable to set initial configuration: " <<
			ConfigKit::toString(errors));
	}
}

static void
loadConfigFile() {
	Json::Reader reader;
	if (!reader.parse(unsafeReadFile("config.json"), testConfig)) {
		fprintf(stderr, "Cannot parse config.json: %s\n",
			reader.getFormattedErrorMessages().c_str());
		exit(1);
	}
}

int
main(int argc, char *argv[]) {
	using namespace Agent::Fundamentals;

	setenv("RAILS_ENV", "production", 1);
	setenv("TESTING_PASSENGER", "1", 1);
	setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
	unsetenv("PASSENGER_USE_FEEDBACK_FD");
	unsetenv("TMPDIR");
	if (getEnvBool("GDB", false) || getEnvBool("LLDB", false)) {
		setenv("PASSENGER_ABORT_HANDLER", "false", 1);
	}

	tut::reporter reporter;
	tut::runner.get().set_callback(&reporter);
	allGroups = tut::runner.get().list_groups();

	ConfigKit::Schema *schema = createSchema();
	ConfigKit::Store *config = new ConfigKit::Store(*schema);
	initializeAgent(argc, &argv, "CxxTestMain", *config,
		ConfigKit::DummyTranslator(), parseOptions);
	resourceLocator = Agent::Fundamentals::context->resourceLocator;
	loadConfigFile();

	bool all_ok = true;
	if (runMode == RUN_ALL_GROUPS) {
		tut::runner.get().run_tests();
	} else {
		try {
			tut::runner.get().run_tests(groupsToRun);
		} catch (const tut::no_such_test &) {
			fprintf(stderr, "ERROR: test not found.\n");
			all_ok = false;
		}
	}
	all_ok = reporter.all_ok();

	shutdownAgent(schema, config);
	if (all_ok) {
		return 0;
	} else {
		return 1;
	}
}

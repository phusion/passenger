#include <TestSupport.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstdio>
#include <cerrno>
#include <ProcessManagement/Spawn.h>
#include <SystemTools/ProcessMetricsCollector.h>
#include <StrIntTools/StrIntUtils.h>

using namespace Passenger;

namespace tut {
	struct SystemTools_ProcessMetricsCollectorTest: public TestBase {
		ProcessMetricsCollector collector;
		pid_t child;

		SystemTools_ProcessMetricsCollectorTest() {
			child = -1;
		}

		~SystemTools_ProcessMetricsCollectorTest() {
			if (child != -1) {
				kill(child, SIGKILL);
				waitpid(child, NULL, 0);
			}
		}

		pid_t spawnChild(int memory) {
			string memoryStr = toString(memory);
			const char *command[] = {
				"../buildout/test/allocate_memory",
				memoryStr.c_str(),
				NULL
			};
			SubprocessInfo info;
			runCommand(command, info, false);
			return info.pid;
		}
	};

	DEFINE_TEST_GROUP(SystemTools_ProcessMetricsCollectorTest);

	TEST_METHOD(1) {
		// It collects the metrics for the given PIDs.
		collector.setPsOutput(
			"  PID  PPID  %CPU    RSS      VSZ  PGID    UID COMMAND\n"
			"    1     0   0.0   1276  2456836     1      0 /sbin/launchd\n"
			"34678  1265  95.2   4128  2437812 34677    123 /bin/bash -li\n"
		);
		vector<pid_t> pids;
		pids.push_back(1);
		pids.push_back(34678);
		ProcessMetricMap result = collector.collect(pids);

		ensure_equals(result.size(), 2u);

		ensure_equals(result[1].pid, (pid_t) 1);
		ensure_equals(result[1].ppid, (pid_t) 0);
		ensure_equals(result[1].cpu, 0u);
		ensure_equals(result[1].rss, 1276u);
		ensure_equals(result[1].processGroupId, (pid_t) 1);
		ensure_equals(result[1].uid, (uid_t) 0);
		ensure_equals(result[1].command, "/sbin/launchd");

		ensure_equals(result[34678].pid, (pid_t) 34678);
		ensure_equals(result[34678].ppid, (pid_t) 1265);
		ensure_equals(result[34678].cpu, 95u);
		ensure_equals(result[34678].rss, 4128u);
		ensure_equals(result[34678].processGroupId, (pid_t) 34677);
		ensure_equals(result[34678].uid, (uid_t) 123);
		ensure_equals(result[34678].command, "/bin/bash -li");
	}

	TEST_METHOD(2) {
		// It does not collect the metrics for PIDs that don't exist.
		collector.setPsOutput(
			"  PID  PPID  %CPU    RSS      VSZ  PGID COMMAND\n"
			"    1     0   0.0   1276  2456836     1 /sbin/launchd\n"
		);
		vector<pid_t> pids;
		pids.push_back(1);
		pids.push_back(34678);
		ProcessMetricMap result = collector.collect(pids);

		ensure_equals(result.size(), 1u);
		ensure(result.find(1) != result.end());
		ensure(result.find(34678) == result.end());
	}

	TEST_METHOD(3) {
		// Measuring real memory usage works.
		ssize_t pss, privateDirty, swap;
		child = spawnChild(50);
		usleep(500000);
		collector.measureRealMemory(child, pss, privateDirty, swap);
		#ifdef __APPLE__
			if (geteuid() == 0) {
				ensure("PSS is correct", pss > 50000 && pss < 100000);
				ensure("Private dirty is correct", privateDirty > 50000 && privateDirty < 100000);
				ensure_equals("Swap is correct", swap, (ssize_t) -1);
			} else {
				ensure_equals("PSS is correct", pss, (ssize_t) -1);
				ensure_equals("Private dirty is correct", privateDirty, (ssize_t) -1);
				ensure_equals("Swap is correct", swap, (ssize_t) -1);
			}
		#elif defined(__linux__)
			ensure("PSS is correct", (pss > 50000 && pss < 60000) || pss == -1);
			ensure("Private dirty is correct", privateDirty > 50000 && privateDirty < 60000);
			ensure("Swap is correct", swap < 10000);
		#else
			ensure("PSS is correct", (pss > 50000 && pss < 60000) || pss == -1);
			ensure("Private dirty is correct", (privateDirty > 50000 && privateDirty < 60000) || privateDirty == -1);
			ensure("Swap is correct", (swap < 10000 || swap == -1));
		#endif
	}
}

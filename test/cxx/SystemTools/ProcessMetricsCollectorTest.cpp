#include <TestSupport.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstdio>
#include <cerrno>
#include <ProcessManagement/Spawn.h>
#include <SystemTools/ProcessMetricsCollector.h>
#include <StrIntTools/StrIntUtils.h>

#if defined(__has_feature)
	#if __has_feature(address_sanitizer)
		#define USING_ASAN 1
	#endif
#endif
#if !defined(USING_ASAN) && defined(__SANITIZE_ADDRESS__)
	#define USING_ASAN 1
#endif

using namespace Passenger;

namespace tut {
	struct SystemTools_ProcessMetricsCollectorTest: public TestBase {
		ProcessMetricsCollector collector;
		pid_t child;

		SystemTools_ProcessMetricsCollectorTest() {
			child = -1;
		}

		~SystemTools_ProcessMetricsCollectorTest() {
			killChild();
		}

		pid_t spawnChild(int memoryMb) {
			string memoryMbStr = toString(memoryMb);
			const char *command[] = {
				"../buildout/test/allocate_memory",
				memoryMbStr.c_str(),
				NULL
			};
			SubprocessInfo info;
			runCommand(command, info, false);
			return info.pid;
		}

		void killChild() {
			if (child != -1) {
				kill(child, SIGKILL);
				waitpid(child, NULL, 0);
				child = -1;
			}
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

		// Sanitizer runtimes add process memory outside the requested allocation.
		// Measure using an otherwise identical child so that this overhead cancels out.
		ssize_t baselinePss, baselinePrivateDirty, baselineSwap;
		child = spawnChild(100);
		usleep(500000);
		collector.measureRealMemory(child, baselinePss,
			baselinePrivateDirty, baselineSwap);
		killChild();

		child = spawnChild(150);
		usleep(500000);
		collector.measureRealMemory(child, pss, privateDirty, swap);

		#ifdef __APPLE__
			if (geteuid() == 0) {
				ensure_gt("PSS is correct: more than 50 MB allocated", pss - baselinePss, 50000);
				#ifdef USING_ASAN
					ensure_lt("PSS is correct: less than 60 MB allocated", pss - baselinePss, 70000);
				#else
					ensure_lt("PSS is correct: less than 60 MB allocated", pss - baselinePss, 60000);
				#endif

				ensure_gt("Private dirty is correct: more than 50 MB allocated", privateDirty - baselinePrivateDirty, 50000);
				ensure_lt("Private dirty is correct: less than 60 MB allocated", privateDirty - baselinePrivateDirty, 60000);
			} else {
				ensure_equals("PSS is cannot be measured without root privileges", pss, (ssize_t) -1);
				ensure_equals("Private dirty is cannot be measured without root privileges", privateDirty, (ssize_t) -1);
			}
			ensure_equals("Swap measurement unsupported (expected)", swap, (ssize_t) -1);
		#else
			if (pss != -1 && baselinePss != -1) {
				ensure_gt("PSS is correct: more than 50 MB allocated", pss - baselinePss, 50000);
				ensure_lt("PSS is correct: less than 60 MB allocated", pss - baselinePss, 60000);
			} else {
				#ifdef __linux__ // Allow measurement failure/non-implementation on other platforms
					fail(("PSS testing failed because one of the values is -1: pss="
						+ to_string(pss) + ", baselinePss=" + to_string(baselinePss)).c_str());
				#endif
			}

			if (privateDirty != -1 && baselinePrivateDirty != -1) {
				ensure_gt("Private dirty is correct: more than 50 MB allocated", privateDirty - baselinePrivateDirty, 50000);
				ensure_lt("Private dirty is correct: less than 60 MB allocated", privateDirty - baselinePrivateDirty, 60000);
			} else {
				#ifdef __linux__ // Allow measurement failure/non-implementation on other platforms
					fail(("Private dirty testing failed because one of the values is -1: privateDirty="
						+ to_string(privateDirty) + ", baselinePrivateDirty=" + to_string(baselinePrivateDirty)).c_str());
				#endif
			}

			ensure("Swap is correct", swap < 10000);
		#endif
	}
}

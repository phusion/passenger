#include "TestSupport.h"
#include "Utils/ProcessMetricsCollector.h"

using namespace Passenger;

namespace tut {
	struct ProcessMetricsCollectorTest {
		ProcessMetricsCollector collector;
	};
	
	DEFINE_TEST_GROUP(ProcessMetricsCollectorTest);
	
	TEST_METHOD(1) {
		// It collects the metrics for the given PIDs.
		collector.setPsOutput(
			"  PID  PPID  %CPU    RSS      VSZ  PGID COMMAND\n"
			"    1     0   0.0   1276  2456836     1 /sbin/launchd\n"
			"34678  1265  95.2   4128  2437812 34677 /bin/bash -li\n"
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
		ensure_equals(result[1].command, "/sbin/launchd");
		
		ensure_equals(result[34678].pid, (pid_t) 34678);
		ensure_equals(result[34678].ppid, (pid_t) 1265);
		ensure_equals(result[34678].cpu, 95u);
		ensure_equals(result[34678].rss, 4128u);
		ensure_equals(result[34678].processGroupId, (pid_t) 34677);
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
}

/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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
#include <Core/ApplicationPool/Pool.h>

/*************************************************************************
 *
 * Analytics collection functions for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


void
Pool::initializeAnalyticsCollection() {
	interruptableThreads.create_thread(
		boost::bind(collectAnalytics, shared_from_this()),
		"Pool analytics collector",
		POOL_HELPER_THREAD_STACK_SIZE
	);
}

void
Pool::collectAnalytics(PoolPtr self) {
	TRACE_POINT();
	syscalls::usleep(3000000);
	while (!boost::this_thread::interruption_requested()) {
		try {
			UPDATE_TRACE_POINT();
			self->realCollectAnalytics();
		} catch (const thread_interrupted &) {
			break;
		} catch (const tracable_exception &e) {
			P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
		}

		UPDATE_TRACE_POINT();
		unsigned long long currentTime = SystemTime::getUsec();
		unsigned long long sleepTime = timeToNextMultipleULL(5000000, currentTime);
		P_DEBUG("Analytics collection done; next analytics collection in " <<
			std::fixed << std::setprecision(3) << (sleepTime / 1000000.0) <<
			" sec");
		try {
			syscalls::usleep(sleepTime);
		} catch (const thread_interrupted &) {
			break;
		} catch (const tracable_exception &e) {
			P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
		}
	}
}

void
Pool::collectPids(const ProcessList &processes, vector<pid_t> &pids) {
	foreach (const ProcessPtr &process, processes) {
		pids.push_back(process->getPid());
	}
}

void
Pool::updateProcessMetrics(const ProcessList &processes,
	const ProcessMetricMap &allMetrics,
	vector<ProcessPtr> &processesToDetach)
{
	foreach (const ProcessPtr &process, processes) {
		ProcessMetricMap::const_iterator metrics_it =
			allMetrics.find(process->getPid());
		if (metrics_it != allMetrics.end()) {
			process->metrics = metrics_it->second;
		// If the process is missing from 'allMetrics' then either 'ps'
		// failed or the process really is gone. We double check by sending
		// it a signal.
		} else if (!process->isDummy() && !process->osProcessExists()) {
			P_WARN("Process " << process->inspect() << " no longer exists! "
				"Detaching it from the pool.");
			processesToDetach.push_back(process);
		}
	}
}

void
Pool::realCollectAnalytics() {
	TRACE_POINT();
	boost::this_thread::disable_interruption di;
	boost::this_thread::disable_syscall_interruption dsi;
	vector<pid_t> pids;
	unsigned int max;

	P_DEBUG("Analytics collection time...");
	// Collect all the PIDs.
	{
		UPDATE_TRACE_POINT();
		LockGuard l(syncher);
		max = this->max;
	}
	pids.reserve(max);
	{
		UPDATE_TRACE_POINT();
		LockGuard l(syncher);
		GroupMap::ConstIterator g_it(groups);

		while (*g_it != NULL) {
			const GroupPtr &group = g_it.getValue();
			collectPids(group->enabledProcesses, pids);
			collectPids(group->disablingProcesses, pids);
			collectPids(group->disabledProcesses, pids);
			g_it.next();
		}
	}

	// Collect process metrics and system and store them in the
	// data structures.
	ProcessMetricMap processMetrics;
	try {
		UPDATE_TRACE_POINT();
		P_DEBUG("Collecting process metrics");
		processMetrics = ProcessMetricsCollector().collect(pids);
	} catch (const ParseException &) {
		P_WARN("Unable to collect process metrics: cannot parse 'ps' output.");
		return;
	}
	try {
		UPDATE_TRACE_POINT();
		P_DEBUG("Collecting system metrics");
		systemMetricsCollector.collect(systemMetrics);
	} catch (const RuntimeException &e) {
		P_WARN("Unable to collect system metrics: " << e.what());
		return;
	}

	{
		UPDATE_TRACE_POINT();
		vector<ProcessPtr> processesToDetach;
		boost::container::vector<Callback> actions;
		ScopedLock l(syncher);
		GroupMap::ConstIterator g_it(groups);

		UPDATE_TRACE_POINT();
		while (*g_it != NULL) {
			const GroupPtr &group = g_it.getValue();
			updateProcessMetrics(group->enabledProcesses, processMetrics, processesToDetach);
			updateProcessMetrics(group->disablingProcesses, processMetrics, processesToDetach);
			updateProcessMetrics(group->disabledProcesses, processMetrics, processesToDetach);
			g_it.next();
		}

		UPDATE_TRACE_POINT();
		foreach (const ProcessPtr process, processesToDetach) {
			detachProcessUnlocked(process, actions);
		}
		UPDATE_TRACE_POINT();
		processesToDetach.clear();

		l.unlock();

		UPDATE_TRACE_POINT();
		runAllActions(actions);
		UPDATE_TRACE_POINT();
		// Run destructors with updated trace point.
		actions.clear();
	}
}


} // namespace ApplicationPool2
} // namespace Passenger

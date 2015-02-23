/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

// This file is included inside the Pool class.

private:

struct UnionStationLogEntry {
	string groupName;
	const char *category;
	string key;
	string data;
};

SystemMetricsCollector systemMetricsCollector;
SystemMetrics systemMetrics;


static void collectAnalytics(PoolPtr self) {
	TRACE_POINT();
	syscalls::usleep(3000000);
	while (!this_thread::interruption_requested()) {
		try {
			UPDATE_TRACE_POINT();
			self->realCollectAnalytics();
		} catch (const thread_interrupted &) {
			break;
		} catch (const tracable_exception &e) {
			P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
		}

		// Sleep for about 4 seconds, aligned to seconds boundary
		// for saving power on laptops.
		UPDATE_TRACE_POINT();
		unsigned long long currentTime = SystemTime::getUsec();
		unsigned long long deadline =
			roundUp<unsigned long long>(currentTime, 1000000) + 4000000;
		P_DEBUG("Analytics collection done; next analytics collection in " <<
			std::fixed << std::setprecision(3) << ((deadline - currentTime) / 1000000.0) <<
			" sec");
		try {
			syscalls::usleep(deadline - currentTime);
		} catch (const thread_interrupted &) {
			break;
		} catch (const tracable_exception &e) {
			P_WARN("ERROR: " << e.what() << "\n  Backtrace:\n" << e.backtrace());
		}
	}
}

static void collectPids(const ProcessList &processes, vector<pid_t> &pids) {
	foreach (const ProcessPtr &process, processes) {
		pids.push_back(process->pid);
	}
}

static void updateProcessMetrics(const ProcessList &processes,
	const ProcessMetricMap &allMetrics,
	vector<ProcessPtr> &processesToDetach)
{
	foreach (const ProcessPtr &process, processes) {
		ProcessMetricMap::const_iterator metrics_it =
			allMetrics.find(process->pid);
		if (metrics_it != allMetrics.end()) {
			process->metrics = metrics_it->second;
		// If the process is missing from 'allMetrics' then either 'ps'
		// failed or the process really is gone. We double check by sending
		// it a signal.
		} else if (!process->dummy && !process->osProcessExists()) {
			P_WARN("Process " << process->inspect() << " no longer exists! "
				"Detaching it from the pool.");
			processesToDetach.push_back(process);
		}
	}
}

void prepareUnionStationProcessStateLogs(vector<UnionStationLogEntry> &logEntries,
	const GroupPtr &group) const
{
	const UnionStation::CorePtr &unionStationCore = getUnionStationCore();
	if (group->options.analytics && unionStationCore != NULL) {
		logEntries.push_back(UnionStationLogEntry());
		UnionStationLogEntry &entry = logEntries.back();
		stringstream stream;

		stream << "Group: <group>";
		group->inspectXml(stream, false);
		stream << "</group>";

		entry.groupName = group->options.getAppGroupName();
		entry.category  = "processes";
		entry.key       = group->options.unionStationKey;
		entry.data      = stream.str();
	}
}

void prepareUnionStationSystemMetricsLogs(vector<UnionStationLogEntry> &logEntries,
	const GroupPtr &group) const
{
	const UnionStation::CorePtr &unionStationCore = getUnionStationCore();
	if (group->options.analytics && unionStationCore != NULL) {
		logEntries.push_back(UnionStationLogEntry());
		UnionStationLogEntry &entry = logEntries.back();
		stringstream stream;

		stream << "System metrics: ";
		systemMetrics.toXml(stream);

		entry.groupName = group->options.getAppGroupName();
		entry.category  = "system_metrics";
		entry.key       = group->options.unionStationKey;
		entry.data      = stream.str();
	}
}

void realCollectAnalytics() {
	TRACE_POINT();
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
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
		SuperGroupMap::ConstIterator sg_it(superGroups);

		while (*sg_it != NULL) {
			const SuperGroupPtr &superGroup = sg_it.getValue();
			SuperGroup::GroupList::const_iterator g_it, g_end = superGroup->groups.end();

			for (g_it = superGroup->groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;
				collectPids(group->enabledProcesses, pids);
				collectPids(group->disablingProcesses, pids);
				collectPids(group->disabledProcesses, pids);
			}
			sg_it.next();
		}
	}

	// Collect process metrics and system and store them in the
	// data structures. Later, we log them to Union Station.
	ProcessMetricMap processMetrics;
	try {
		UPDATE_TRACE_POINT();
		processMetrics = ProcessMetricsCollector().collect(pids);
	} catch (const ParseException &) {
		P_WARN("Unable to collect process metrics: cannot parse 'ps' output.");
		return;
	}
	try {
		UPDATE_TRACE_POINT();
		systemMetricsCollector.collect(systemMetrics);
	} catch (const RuntimeException &e) {
		P_WARN("Unable to collect system metrics: " << e.what());
		return;
	}

	{
		UPDATE_TRACE_POINT();
		vector<UnionStationLogEntry> logEntries;
		vector<ProcessPtr> processesToDetach;
		boost::container::vector<Callback> actions;
		ScopedLock l(syncher);
		SuperGroupMap::ConstIterator sg_it(superGroups);

		UPDATE_TRACE_POINT();
		while (*sg_it != NULL) {
			const SuperGroupPtr &superGroup = sg_it.getValue();
			SuperGroup::GroupList::iterator g_it, g_end = superGroup->groups.end();

			for (g_it = superGroup->groups.begin(); g_it != g_end; g_it++) {
				const GroupPtr &group = *g_it;

				updateProcessMetrics(group->enabledProcesses, processMetrics, processesToDetach);
				updateProcessMetrics(group->disablingProcesses, processMetrics, processesToDetach);
				updateProcessMetrics(group->disabledProcesses, processMetrics, processesToDetach);
				prepareUnionStationProcessStateLogs(logEntries, group);
				prepareUnionStationSystemMetricsLogs(logEntries, group);
			}
			sg_it.next();
		}

		UPDATE_TRACE_POINT();
		foreach (const ProcessPtr process, processesToDetach) {
			detachProcessUnlocked(process, actions);
		}
		UPDATE_TRACE_POINT();
		processesToDetach.clear();

		l.unlock();
		UPDATE_TRACE_POINT();
		if (!logEntries.empty()) {
			const UnionStation::CorePtr &unionStationCore = getUnionStationCore();
			while (!logEntries.empty()) {
				UnionStationLogEntry &entry = logEntries.back();
				UnionStation::TransactionPtr transaction =
					unionStationCore->newTransaction(
						entry.groupName,
						entry.category,
						entry.key);
				transaction->message(entry.data);
				logEntries.pop_back();
			}
		}

		UPDATE_TRACE_POINT();
		runAllActions(actions);
		UPDATE_TRACE_POINT();
		// Run destructors with updated trace point.
		actions.clear();
	}
}


protected:

void initializeAnalyticsCollection() {
	interruptableThreads.create_thread(
		boost::bind(collectAnalytics, shared_from_this()),
		"Pool analytics collector",
		POOL_HELPER_THREAD_STACK_SIZE
	);
}

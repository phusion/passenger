Process *
findProcessWithStickySessionId(unsigned int id) const {
	ProcessList::const_iterator it, end = enabledProcesses.end();
	for (it = enabledProcesses.begin(); it != end; it++) {
		Process *process = it->get();
		if (process->stickySessionId == id) {
			return process;
		}
	}
	return NULL;
}

Process *
findProcessWithStickySessionIdOrLowestBusyness(unsigned int id) const {
	int leastBusyProcessIndex = -1;
	int lowestBusyness = 0;
	unsigned int i, size = enabledProcessBusynessLevels.size();
	const int *enabledProcessBusynessLevels = &this->enabledProcessBusynessLevels[0];

	for (i = 0; i < size; i++) {
		Process *process = enabledProcesses[i].get();
		if (process->stickySessionId == id) {
			return process;
		} else if (leastBusyProcessIndex == -1 || enabledProcessBusynessLevels[i] < lowestBusyness) {
			leastBusyProcessIndex = i;
			lowestBusyness = enabledProcessBusynessLevels[i];
		}
	}

	if (leastBusyProcessIndex == -1) {
		return NULL;
	} else {
		return enabledProcesses[leastBusyProcessIndex].get();
	}
}

Process *
findProcessWithLowestBusyness(const ProcessList &processes) const {
	if (processes.empty()) {
		return NULL;
	}

	int leastBusyProcessIndex = -1;
	int lowestBusyness = 0;
	unsigned int i, size = enabledProcessBusynessLevels.size();
	const int *enabledProcessBusynessLevels = &this->enabledProcessBusynessLevels[0];

	for (i = 0; i < size; i++) {
		if (leastBusyProcessIndex == -1 || enabledProcessBusynessLevels[i] < lowestBusyness) {
			leastBusyProcessIndex = i;
			lowestBusyness = enabledProcessBusynessLevels[i];
		}
	}
	return enabledProcesses[leastBusyProcessIndex].get();
}

/**
 * Removes a process to the given list (enabledProcess, disablingProcesses, disabledProcesses).
 * This function does not fix getWaitlist invariants or other stuff.
 */
void
removeProcessFromList(const ProcessPtr &process, ProcessList &source) {
	ProcessPtr p = process; // Keep an extra reference count just in case.

	source.erase(source.begin() + process->index);
	process->index = -1;

	switch (process->enabled) {
	case Process::ENABLED:
		assert(&source == &enabledProcesses);
		enabledCount--;
		if (process->isTotallyBusy()) {
			nEnabledProcessesTotallyBusy--;
		}
		break;
	case Process::DISABLING:
		assert(&source == &disablingProcesses);
		disablingCount--;
		break;
	case Process::DISABLED:
		assert(&source == &disabledProcesses);
		disabledCount--;
		break;
	case Process::DETACHED:
		assert(&source == &detachedProcesses);
		break;
	default:
		P_BUG("Unknown 'enabled' state " << (int) process->enabled);
	}

	// Rebuild indices
	ProcessList::iterator it, end = source.end();
	unsigned int i = 0;
	for (it = source.begin(); it != end; it++, i++) {
		const ProcessPtr &process = *it;
		process->index = i;
	}

	// Rebuild enabledProcessBusynessLevels
	if (&source == &enabledProcesses) {
		enabledProcessBusynessLevels.clear();
		for (it = source.begin(); it != end; it++, i++) {
			const ProcessPtr &process = *it;
			enabledProcessBusynessLevels.push_back(process->busyness());
		}
		enabledProcessBusynessLevels.shrink_to_fit();
	}
}

/**
 * Adds a process to the given list (enabledProcess, disablingProcesses, disabledProcesses)
 * and sets the process->enabled flag accordingly.
 * The process must currently not be in any list. This function does not fix
 * getWaitlist invariants or other stuff.
 */
void
addProcessToList(const ProcessPtr &process, ProcessList &destination) {
	destination.push_back(process);
	process->index = destination.size() - 1;
	if (&destination == &enabledProcesses) {
		process->enabled = Process::ENABLED;
		enabledCount++;
		enabledProcessBusynessLevels.push_back(process->busyness());
		if (process->isTotallyBusy()) {
			nEnabledProcessesTotallyBusy++;
		}
	} else if (&destination == &disablingProcesses) {
		process->enabled = Process::DISABLING;
		disablingCount++;
	} else if (&destination == &disabledProcesses) {
		assert(process->sessions == 0);
		process->enabled = Process::DISABLED;
		disabledCount++;
	} else if (&destination == &detachedProcesses) {
		assert(process->isAlive());
		process->enabled = Process::DETACHED;
		callAbortLongRunningConnectionsCallback(process);
	} else {
		P_BUG("Unknown destination list");
	}
}

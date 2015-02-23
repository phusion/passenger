ProcessPtr findOldestIdleProcess(const Group *exclude = NULL) const {
	ProcessPtr oldestIdleProcess;

	SuperGroupMap::ConstIterator sg_it(superGroups);
	while (*sg_it != NULL) {
		const SuperGroupPtr &superGroup = sg_it.getValue();
		const SuperGroup::GroupList &groups = superGroup->groups;
		SuperGroup::GroupList::const_iterator g_it, g_end = groups.end();
		for (g_it = groups.begin(); g_it != g_end; g_it++) {
			const GroupPtr &group = *g_it;
			if (group.get() == exclude) {
				continue;
			}
			const ProcessList &processes = group->enabledProcesses;
			ProcessList::const_iterator p_it, p_end = processes.end();
			for (p_it = processes.begin(); p_it != p_end; p_it++) {
				const ProcessPtr process = *p_it;
				if (process->busyness() == 0
				     && (oldestIdleProcess == NULL
				         || process->lastUsed < oldestIdleProcess->lastUsed)
				) {
					oldestIdleProcess = process;
				}
			}
		}
		sg_it.next();
	}

	return oldestIdleProcess;
}

ProcessPtr findBestProcessToTrash() const {
	ProcessPtr oldestProcess;

	SuperGroupMap::ConstIterator sg_it(superGroups);
	while (*sg_it != NULL) {
		const SuperGroupPtr &superGroup = sg_it.getValue();
		const SuperGroup::GroupList &groups = superGroup->groups;
		SuperGroup::GroupList::const_iterator g_it, g_end = groups.end();
		for (g_it = groups.begin(); g_it != g_end; g_it++) {
			const GroupPtr &group = *g_it;
			const ProcessList &processes = group->enabledProcesses;
			ProcessList::const_iterator p_it, p_end = processes.end();
			for (p_it = processes.begin(); p_it != p_end; p_it++) {
				const ProcessPtr process = *p_it;
				if (oldestProcess == NULL
				 || process->lastUsed < oldestProcess->lastUsed) {
					oldestProcess = process;
				}
			}
		}
		sg_it.next();
	}

	return oldestProcess;
}

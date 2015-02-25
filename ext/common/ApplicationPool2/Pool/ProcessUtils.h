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

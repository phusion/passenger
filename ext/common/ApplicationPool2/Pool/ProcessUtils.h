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

	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (group.get() == exclude) {
			g_it.next();
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
		g_it.next();
	}

	return oldestIdleProcess;
}

ProcessPtr findBestProcessToTrash() const {
	ProcessPtr oldestProcess;

	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		const ProcessList &processes = group->enabledProcesses;
		ProcessList::const_iterator p_it, p_end = processes.end();
		for (p_it = processes.begin(); p_it != p_end; p_it++) {
			const ProcessPtr process = *p_it;
			if (oldestProcess == NULL
			 || process->lastUsed < oldestProcess->lastUsed) {
				oldestProcess = process;
			}
		}
		g_it.next();
	}

	return oldestProcess;
}

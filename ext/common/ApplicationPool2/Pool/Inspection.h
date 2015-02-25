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

public:

struct InspectOptions {
	bool colorize;
	bool verbose;

	InspectOptions()
		: colorize(false),
		  verbose(false)
		{ }

	InspectOptions(const VariantMap &options)
		: colorize(options.getBool("colorize", false, false)),
		  verbose(options.getBool("verbose", false, false))
		{ }
};


private:

void inspectProcessList(const InspectOptions &options, stringstream &result,
	const Group *group, const ProcessList &processes) const
{
	ProcessList::const_iterator p_it;
	for (p_it = processes.begin(); p_it != processes.end(); p_it++) {
		const ProcessPtr &process = *p_it;
		char buf[128];
		char cpubuf[10];
		char membuf[10];

		snprintf(cpubuf, sizeof(cpubuf), "%d%%", (int) process->metrics.cpu);
		snprintf(membuf, sizeof(membuf), "%ldM",
			(unsigned long) (process->metrics.realMemory() / 1024));
		snprintf(buf, sizeof(buf),
				"  * PID: %-5lu   Sessions: %-2u      Processed: %-5u   Uptime: %s\n"
				"    CPU: %-5s   Memory  : %-5s   Last used: %s ago",
				(unsigned long) process->pid,
				process->sessions,
				process->processed,
				process->uptime().c_str(),
				cpubuf,
				membuf,
				distanceOfTimeInWords(process->lastUsed / 1000000).c_str());
		result << buf << endl;

		if (process->enabled == Process::DISABLING) {
			result << "    Disabling..." << endl;
		} else if (process->enabled == Process::DISABLED) {
			result << "    DISABLED" << endl;
		} else if (process->enabled == Process::DETACHED) {
			result << "    Shutting down..." << endl;
		}

		const Socket *socket;
		if (options.verbose && (socket = process->sockets.findSocketWithName("http")) != NULL) {
			result << "    URL     : http://" << replaceString(socket->address, "tcp://", "") << endl;
			result << "    Password: " << StaticString(group->secret, Group::SECRET_SIZE) << endl;
		}
	}
}

static const char *maybeColorize(const InspectOptions &options, const char *color) {
	if (options.colorize) {
		return color;
	} else {
		return "";
	}
}


public:

string inspect(const InspectOptions &options = InspectOptions(), bool lock = true) const {
	DynamicScopedLock l(syncher, lock);
	stringstream result;
	const char *headerColor = maybeColorize(options, ANSI_COLOR_YELLOW ANSI_COLOR_BLUE_BG ANSI_COLOR_BOLD);
	const char *resetColor  = maybeColorize(options, ANSI_COLOR_RESET);

	result << headerColor << "----------- General information -----------" << resetColor << endl;
	result << "Max pool size : " << max << endl;
	result << "Processes     : " << getProcessCount(false) << endl;
	result << "Requests in top-level queue : " << getWaitlist.size() << endl;
	if (options.verbose) {
		unsigned int i = 0;
		foreach (const GetWaiter &waiter, getWaitlist) {
			result << "  " << i << ": " << waiter.options.getAppGroupName() << endl;
			i++;
		}
	}
	result << endl;

	result << headerColor << "----------- Application groups -----------" << resetColor << endl;
	SuperGroupMap::ConstIterator sg_it(superGroups);
	while (*sg_it != NULL) {
		const SuperGroupPtr &superGroup = sg_it.getValue();
		const Group *group = superGroup->defaultGroup;
		ProcessList::const_iterator p_it;

		if (group != NULL) {
			result << group->name << ":" << endl;
			result << "  App root: " << group->options.appRoot << endl;
			if (group->restarting()) {
				result << "  (restarting...)" << endl;
			}
			if (group->spawning()) {
				if (group->processesBeingSpawned == 0) {
					result << "  (spawning...)" << endl;
				} else {
					result << "  (spawning " << group->processesBeingSpawned << " new " <<
						maybePluralize(group->processesBeingSpawned, "process", "processes") <<
						"...)" << endl;
				}
			}
			result << "  Requests in queue: " << group->getWaitlist.size() << endl;
			inspectProcessList(options, result, group, group->enabledProcesses);
			inspectProcessList(options, result, group, group->disablingProcesses);
			inspectProcessList(options, result, group, group->disabledProcesses);
			inspectProcessList(options, result, group, group->detachedProcesses);
			result << endl;
		}
		sg_it.next();
	}
	return result.str();
}

string toXml(bool includeSecrets = true, bool lock = true) const {
	DynamicScopedLock l(syncher, lock);
	stringstream result;
	SuperGroupMap::ConstIterator sg_it(superGroups);
	SuperGroup::GroupList::const_iterator g_it;
	ProcessList::const_iterator p_it;

	result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
	result << "<info version=\"3\">";

	result << "<passenger_version>" << PASSENGER_VERSION << "</passenger_version>";
	result << "<process_count>" << getProcessCount(false) << "</process_count>";
	result << "<max>" << max << "</max>";
	result << "<capacity_used>" << capacityUsedUnlocked() << "</capacity_used>";
	result << "<get_wait_list_size>" << getWaitlist.size() << "</get_wait_list_size>";

	if (includeSecrets) {
		vector<GetWaiter>::const_iterator w_it, w_end = getWaitlist.end();

		result << "<get_wait_list>";
		for (w_it = getWaitlist.begin(); w_it != w_end; w_it++) {
			const GetWaiter &waiter = *w_it;
			result << "<item>";
			result << "<app_group_name>" << escapeForXml(waiter.options.getAppGroupName()) << "</app_group_name>";
			result << "</item>";
		}
		result << "</get_wait_list>";
	}

	result << "<supergroups>";
	while (*sg_it != NULL) {
		const SuperGroupPtr &superGroup = sg_it.getValue();

		result << "<supergroup>";
		result << "<name>" << escapeForXml(superGroup->name) << "</name>";
		result << "<state>" << superGroup->getStateName() << "</state>";
		result << "<get_wait_list_size>" << superGroup->getWaitlist.size() << "</get_wait_list_size>";
		result << "<capacity_used>" << superGroup->capacityUsed() << "</capacity_used>";
		if (includeSecrets) {
			result << "<secret>" << escapeForXml(superGroup->secret) << "</secret>";
		}

		for (g_it = superGroup->groups.begin(); g_it != superGroup->groups.end(); g_it++) {
			const GroupPtr &group = *g_it;

			if (group->componentInfo.isDefault) {
				result << "<group default=\"true\">";
			} else {
				result << "<group>";
			}
			group->inspectXml(result, includeSecrets);
			result << "</group>";
		}
		result << "</supergroup>";

		sg_it.next();
	}
	result << "</supergroups>";

	result << "</info>";
	return result.str();
}

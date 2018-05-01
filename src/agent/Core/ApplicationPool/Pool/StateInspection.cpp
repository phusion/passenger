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
 * State inspection functions for ApplicationPool2::Pool
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


/****************************
 *
 * Private methods
 *
 ****************************/


unsigned int
Pool::capacityUsedUnlocked() const {
	if (groups.size() == 1) {
		GroupPtr *group;
		groups.lookupRandom(NULL, &group);
		return (*group)->capacityUsed();
	} else {
		GroupMap::ConstIterator g_it(groups);
		int result = 0;
		while (*g_it != NULL) {
			const GroupPtr &group = g_it.getValue();
			result += group->capacityUsed();
			g_it.next();
		}
		return result;
	}
}

bool
Pool::atFullCapacityUnlocked() const {
	return capacityUsedUnlocked() >= max;
}

void
Pool::inspectProcessList(const InspectOptions &options, stringstream &result,
	const Group *group, const ProcessList &processes) const
{
	ProcessList::const_iterator p_it;
	for (p_it = processes.begin(); p_it != processes.end(); p_it++) {
		const ProcessPtr &process = *p_it;
		char buf[128];
		char cpubuf[10];
		char membuf[10];

		 if (process->metrics.isValid()) {
			snprintf(cpubuf, sizeof(cpubuf), "%d%%", (int) process->metrics.cpu);
			snprintf(membuf, sizeof(membuf), "%ldM",
				(unsigned long) (process->metrics.realMemory() / 1024));
		} else {
			snprintf(cpubuf, sizeof(cpubuf), "0%%");
			snprintf(membuf, sizeof(membuf), "0M");
		}
		snprintf(buf, sizeof(buf),
			"  * PID: %-5lu   Sessions: %-2u      Processed: %-5u   Uptime: %s\n"
			"    CPU: %-5s   Memory  : %-5s   Last used: %s ago",
			(unsigned long) process->getPid(),
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
		if (options.verbose && (socket = process->getSockets().findFirstSocketWithProtocol("http")) != NULL) {
			result << "    URL     : http://" << replaceString(socket->address, "tcp://", "") << endl;
			result << "    Password: " << group->getApiKey().toStaticString() << endl;
		}
	}
}


/****************************
 *
 * Public methods
 *
 ****************************/


string
Pool::inspect(const InspectOptions &options, bool lock) const {
	DynamicScopedLock l(syncher, lock);
	stringstream result;
	const char *headerColor = maybeColorize(options, ANSI_COLOR_YELLOW ANSI_COLOR_BLUE_BG ANSI_COLOR_BOLD);
	const char *resetColor  = maybeColorize(options, ANSI_COLOR_RESET);

	if (!authorizeByUid(options.uid, false)
	 && !authorizeByApiKey(options.apiKey, false))
	{
		throw SecurityException("Operation unauthorized");
	}

	result << headerColor << "----------- General information -----------" << resetColor << endl;
	result << "Max pool size : " << max << endl;
	result << "App groups    : " << groups.size() << endl;
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
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (!group->authorizeByUid(options.uid)
		 && !group->authorizeByApiKey(options.apiKey))
		{
			g_it.next();
			continue;
		}

		ProcessList::const_iterator p_it;

		result << group->getName() << ":" << endl;
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
		inspectProcessList(options, result, group.get(), group->enabledProcesses);
		inspectProcessList(options, result, group.get(), group->disablingProcesses);
		inspectProcessList(options, result, group.get(), group->disabledProcesses);
		inspectProcessList(options, result, group.get(), group->detachedProcesses);
		result << endl;

		g_it.next();
	}
	return result.str();
}

string
Pool::toXml(const ToXmlOptions &options, bool lock) const {
	DynamicScopedLock l(syncher, lock);
	stringstream result;
	GroupMap::ConstIterator g_it(groups);
	ProcessList::const_iterator p_it;

	if (!authorizeByUid(options.uid, false)
	 && !authorizeByApiKey(options.apiKey, false))
	{
		throw SecurityException("Operation unauthorized");
	}

	result << "<?xml version=\"1.0\" encoding=\"iso8859-1\" ?>\n";
	result << "<info version=\"3\">";

	result << "<passenger_version>" << PASSENGER_VERSION << "</passenger_version>";
	result << "<group_count>" << groups.size() << "</group_count>";
	result << "<process_count>" << getProcessCount(false) << "</process_count>";
	result << "<max>" << max << "</max>";
	result << "<capacity_used>" << capacityUsedUnlocked() << "</capacity_used>";
	result << "<get_wait_list_size>" << getWaitlist.size() << "</get_wait_list_size>";

	if (options.secrets) {
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
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		if (!group->authorizeByUid(options.uid)
		 && !group->authorizeByApiKey(options.apiKey))
		{
			g_it.next();
			continue;
		}

		result << "<supergroup>";
		result << "<name>" << escapeForXml(group->getName()) << "</name>";
		result << "<state>READY</state>";
		result << "<get_wait_list_size>0</get_wait_list_size>";
		result << "<capacity_used>" << group->capacityUsed() << "</capacity_used>";
		if (options.secrets) {
			result << "<secret>" << escapeForXml(group->getApiKey().toStaticString()) << "</secret>";
		}

		result << "<group default=\"true\">";
		group->inspectXml(result, options.secrets);
		result << "</group>";

		result << "</supergroup>";

		g_it.next();
	}
	result << "</supergroups>";

	result << "</info>";
	return result.str();
}

Json::Value
Pool::inspectPropertiesInAdminPanelFormat(const ToJsonOptions &options) const {
	ScopedLock l(syncher);
	Json::Value result(Json::objectValue);
	GroupMap::ConstIterator g_it(groups);
	ProcessList::const_iterator p_it;

	if (!authorizeByUid(options.uid, false)
	 && !authorizeByApiKey(options.apiKey, false))
	{
		throw SecurityException("Operation unauthorized");
	}

	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();

		if (options.hasApplicationIdsFilter) {
			const bool *tmp;
			if (!options.applicationIdsFilter.lookup(group->info.name, &tmp)) {
				g_it.next();
				continue;
			}
		}

		if (!group->authorizeByUid(options.uid)
		 && !group->authorizeByApiKey(options.apiKey))
		{
			g_it.next();
			continue;
		}

		Json::Value groupDoc(Json::objectValue);
		group->inspectPropertiesInAdminPanelFormat(groupDoc);
		result[group->info.name] = groupDoc;

		g_it.next();
	}

	return result;
}

Json::Value
Pool::inspectConfigInAdminPanelFormat(const ToJsonOptions &options) const {
	ScopedLock l(syncher);
	Json::Value result(Json::objectValue);
	GroupMap::ConstIterator g_it(groups);
	ProcessList::const_iterator p_it;

	if (!authorizeByUid(options.uid, false)
	 && !authorizeByApiKey(options.apiKey, false))
	{
		throw SecurityException("Operation unauthorized");
	}

	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();

		if (options.hasApplicationIdsFilter) {
			const bool *tmp;
			if (!options.applicationIdsFilter.lookup(group->info.name, &tmp)) {
				g_it.next();
				continue;
			}
		}

		if (!group->authorizeByUid(options.uid)
		 && !group->authorizeByApiKey(options.apiKey))
		{
			g_it.next();
			continue;
		}

		Json::Value groupDoc(Json::objectValue);
		group->inspectConfigInAdminPanelFormat(groupDoc);
		result[group->info.name] = groupDoc;

		g_it.next();
	}

	return result;
}


Json::Value
Pool::makeSingleValueJsonConfigFormat(const Json::Value &val, const Json::Value &defaultValue) {
	Json::Value ary(Json::arrayValue);

	if (val != defaultValue) {
		Json::Value entry;

		entry["value"] = val;
		entry["source"]["type"] = "ephemeral";

		ary.append(entry);
	}

	if (!defaultValue.isNull()) {
		Json::Value entry;

		entry["value"] = defaultValue;
		entry["source"]["type"] = "default";

		ary.append(entry);
	}

	return ary;
}

Json::Value
Pool::makeSingleStrValueJsonConfigFormat(const StaticString &val) {
	return makeSingleValueJsonConfigFormat(
		Json::Value(val.data(), val.data() + val.size()));
}

Json::Value
Pool::makeSingleStrValueJsonConfigFormat(const StaticString &val, const StaticString &defaultValue) {
	return makeSingleValueJsonConfigFormat(
		Json::Value(val.data(), val.data() + val.size()),
		Json::Value(defaultValue.data(), defaultValue.data() + defaultValue.size()));
}

Json::Value
Pool::makeSingleNonEmptyStrValueJsonConfigFormat(const StaticString &val) {
	if (val.empty()) {
		return Json::arrayValue;
	} else {
		return makeSingleStrValueJsonConfigFormat(val);
	}
}

unsigned int
Pool::capacityUsed() const {
	LockGuard l(syncher);
	return capacityUsedUnlocked();
}

bool
Pool::atFullCapacity() const {
	LockGuard l(syncher);
	return atFullCapacityUnlocked();
}

/**
 * Returns the total number of processes in the pool, including all disabling and
 * disabled processes, but excluding processes that are shutting down and excluding
 * processes that are being spawned.
 */
unsigned int
Pool::getProcessCount(bool lock) const {
	DynamicScopedLock l(syncher, lock);
	unsigned int result = 0;
	GroupMap::ConstIterator g_it(groups);
	while (*g_it != NULL) {
		const GroupPtr &group = g_it.getValue();
		result += group->getProcessCount();
		g_it.next();
	}
	return result;
}

unsigned int
Pool::getGroupCount() const {
	LockGuard l(syncher);
	return groups.size();
}


} // namespace ApplicationPool2
} // namespace Passenger

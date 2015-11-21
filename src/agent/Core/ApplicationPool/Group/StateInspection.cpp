/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion Holding B.V.
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
#include <Core/ApplicationPool/Group.h>

/*************************************************************************
 *
 * Session management functions for ApplicationPool2::Group
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


/****************************
 *
 * Public methods
 *
 ****************************/


unsigned int
Group::getProcessCount() const {
	return enabledCount + disablingCount + disabledCount;
}

/**
 * Returns whether the lower bound of the group-specific process limits
 * have been satisfied. Note that even if the result is false, the pool limits
 * may not allow spawning, so you should check `pool->atFullCapacity()` too.
 */
bool
Group::processLowerLimitsSatisfied() const {
	return capacityUsed() >= options.minProcesses;
}

/**
 * Returns whether the upper bound of the group-specific process limits have
 * been reached, or surpassed. Does not check whether pool limits have been
 * reached. Use `pool->atFullCapacity()` to check for that.
 */
bool
Group::processUpperLimitsReached() const {
	return options.maxProcesses != 0 && capacityUsed() >= options.maxProcesses;
}

/**
 * Returns whether all enabled processes are totally busy. If so, another
 * process should be spawned, if allowed by the process limits.
 * Returns false if there are no enabled processes.
 */
bool
Group::allEnabledProcessesAreTotallyBusy() const {
	return nEnabledProcessesTotallyBusy == enabledCount;
}

/**
 * Returns the number of processes in this group that should be part of the
 * ApplicationPool process limits calculations.
 */
unsigned int
Group::capacityUsed() const {
	return enabledCount + disablingCount + disabledCount + processesBeingSpawned;
}

/**
 * Checks whether this group is waiting for capacity on the pool to
 * become available before it can continue processing requests.
 */
bool
Group::isWaitingForCapacity() const {
	return enabledProcesses.empty()
		&& processesBeingSpawned == 0
		&& !m_restarting
		&& !getWaitlist.empty();
}

bool
Group::garbageCollectable(unsigned long long now) const {
	/* if (now == 0) {
		now = SystemTime::getUsec();
	}
	return busyness() == 0
		&& getWaitlist.empty()
		&& disabledProcesses.empty()
		&& options.getMaxPreloaderIdleTime() != 0
		&& now - spawner->lastUsed() >
			(unsigned long long) options.getMaxPreloaderIdleTime() * 1000000; */
	return false;
}

void
Group::inspectXml(std::ostream &stream, bool includeSecrets) const {
	ProcessList::const_iterator it;

	stream << "<name>" << escapeForXml(info.name) << "</name>";
	stream << "<component_name>" << escapeForXml(info.name) << "</component_name>";
	stream << "<app_root>" << escapeForXml(options.appRoot) << "</app_root>";
	stream << "<app_type>" << escapeForXml(options.appType) << "</app_type>";
	stream << "<environment>" << escapeForXml(options.environment) << "</environment>";
	stream << "<uuid>" << toString(uuid) << "</uuid>";
	stream << "<enabled_process_count>" << enabledCount << "</enabled_process_count>";
	stream << "<disabling_process_count>" << disablingCount << "</disabling_process_count>";
	stream << "<disabled_process_count>" << disabledCount << "</disabled_process_count>";
	stream << "<capacity_used>" << capacityUsed() << "</capacity_used>";
	stream << "<get_wait_list_size>" << getWaitlist.size() << "</get_wait_list_size>";
	stream << "<disable_wait_list_size>" << disableWaitlist.size() << "</disable_wait_list_size>";
	stream << "<processes_being_spawned>" << processesBeingSpawned << "</processes_being_spawned>";
	if (m_spawning) {
		stream << "<spawning/>";
	}
	if (restarting()) {
		stream << "<restarting/>";
	}
	if (includeSecrets) {
		stream << "<secret>" << escapeForXml(getApiKey().toStaticString()) << "</secret>";
		stream << "<api_key>" << escapeForXml(getApiKey().toStaticString()) << "</api_key>";
	}
	LifeStatus lifeStatus = (LifeStatus) this->lifeStatus.load(boost::memory_order_relaxed);
	switch (lifeStatus) {
	case ALIVE:
		stream << "<life_status>ALIVE</life_status>";
		break;
	case SHUTTING_DOWN:
		stream << "<life_status>SHUTTING_DOWN</life_status>";
		break;
	case SHUT_DOWN:
		stream << "<life_status>SHUT_DOWN</life_status>";
		break;
	default:
		P_BUG("Unknown 'lifeStatus' state " << lifeStatus);
	}

	SpawningKit::UserSwitchingInfo usInfo(SpawningKit::prepareUserSwitching(options));
	stream << "<user>" << escapeForXml(usInfo.username) << "</user>";
	stream << "<uid>" << usInfo.uid << "</uid>";
	stream << "<group>" << escapeForXml(usInfo.groupname) << "</group>";
	stream << "<gid>" << usInfo.gid << "</gid>";

	stream << "<options>";
	options.toXml(stream, getResourceLocator());
	stream << "</options>";

	stream << "<processes>";

	for (it = enabledProcesses.begin(); it != enabledProcesses.end(); it++) {
		stream << "<process>";
		(*it)->inspectXml(stream, includeSecrets);
		stream << "</process>";
	}
	for (it = disablingProcesses.begin(); it != disablingProcesses.end(); it++) {
		stream << "<process>";
		(*it)->inspectXml(stream, includeSecrets);
		stream << "</process>";
	}
	for (it = disabledProcesses.begin(); it != disabledProcesses.end(); it++) {
		stream << "<process>";
		(*it)->inspectXml(stream, includeSecrets);
		stream << "</process>";
	}
	for (it = detachedProcesses.begin(); it != detachedProcesses.end(); it++) {
		stream << "<process>";
		(*it)->inspectXml(stream, includeSecrets);
		stream << "</process>";
	}

	stream << "</processes>";
}


} // namespace ApplicationPool2
} // namespace Passenger

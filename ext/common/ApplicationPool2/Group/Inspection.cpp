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

// This file is included inside the Group class.

public:

template<typename Stream>
void
inspectXml(Stream &stream, bool includeSecrets = true) const {
	ProcessList::const_iterator it;

	stream << "<name>" << escapeForXml(name) << "</name>";
	stream << "<component_name>" << escapeForXml(name) << "</component_name>";
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
		stream << "<secret>" << escapeForXml(StaticString(secret, SECRET_SIZE)) << "</secret>";
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

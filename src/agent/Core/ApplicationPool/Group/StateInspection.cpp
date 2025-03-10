/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2025 Asynchronous B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Asynchronous B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL_GROUP_STATE_INSPECTION_CPP_
#define _PASSENGER_APPLICATION_POOL_GROUP_STATE_INSPECTION_CPP_

#ifdef INTELLISENSE
	#include <Core/ApplicationPool/Pool.h>
#endif
#include <Core/ApplicationPool/Group.h>
#include <FileTools/PathManip.h>
#include <cassert>
#include <modp_b64.h>

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
	// check maxInstances limit as set by Enterprise (OSS maxInstancesPerApp piggybacks on this,
	// see InitRequest.cpp)
	return options.maxProcesses != 0 && capacityUsed() >= options.maxProcesses;
}

/**
 * Returns whether all enabled processes are totally busy. If so, another
 * process should be spawned, if allowed by the process limits.
 * Returns false if there are no enabled processes.
 */
bool
Group::allEnabledProcessesAreTotallyBusy() const {
	return nEnabledProcessesTotallyBusy == enabledCount && enabledCount > 0;
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

	SpawningKit::UserSwitchingInfo usInfo(SpawningKit::prepareUserSwitching(options,
		getWrapperRegistry()));
	stream << "<user>" << escapeForXml(usInfo.username) << "</user>";
	stream << "<uid>" << usInfo.uid << "</uid>";
	stream << "<group>" << escapeForXml(usInfo.groupname) << "</group>";
	stream << "<gid>" << usInfo.gid << "</gid>";

	stream << "<options>";
	options.toXml(stream, getResourceLocator(), getWrapperRegistry());
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

void
Group::inspectPropertiesInAdminPanelFormat(Json::Value &result) const {
	result["path"] = absolutizePath(options.appRoot);
	result["startup_file"] = absolutizePath(options.getStartupFile(getWrapperRegistry()),
		absolutizePath(options.appRoot));
	result["start_command"] = options.getStartCommand(getResourceLocator(),
		getWrapperRegistry());
	result["type"] = getWrapperRegistry().lookup(options.appType).language.toString();

	SpawningKit::UserSwitchingInfo usInfo(SpawningKit::prepareUserSwitching(options,
		getWrapperRegistry()));
	result["user"]["username"] = usInfo.username;
	result["user"]["uid"] = (Json::Int) usInfo.uid;
	result["group"]["groupname"] = usInfo.groupname;
	result["group"]["gid"] = (Json::Int) usInfo.gid;

	/******************/
}

void
Group::inspectConfigInAdminPanelFormat(Json::Value &result) const {
	#define VAL Pool::makeSingleValueJsonConfigFormat
	#define SVAL Pool::makeSingleStrValueJsonConfigFormat
	#define NON_EMPTY_SVAL Pool::makeSingleNonEmptyStrValueJsonConfigFormat

	result["app_root"] = NON_EMPTY_SVAL(absolutizePath(options.appRoot));
	result["app_group_name"] = NON_EMPTY_SVAL(info.name);
	result["default_user"] = NON_EMPTY_SVAL(options.defaultUser);
	result["default_group"] = NON_EMPTY_SVAL(options.defaultGroup);
	result["enabled"] = VAL(true, false);
	result["lve_min_uid"] = VAL(options.lveMinUid, DEFAULT_LVE_MIN_UID);

	result["type"] = NON_EMPTY_SVAL(options.appType);
	result["startup_file"] = NON_EMPTY_SVAL(options.startupFile);
	result["start_command"] = NON_EMPTY_SVAL(replaceAll(options.appStartCommand,
		P_STATIC_STRING("\t"), P_STATIC_STRING(" ")));
	result["ruby"] = SVAL(options.ruby, DEFAULT_RUBY);
	result["python"] = SVAL(options.python, DEFAULT_PYTHON);
	result["nodejs"] = SVAL(options.nodejs, DEFAULT_NODEJS);
	result["meteor_app_settings"] = NON_EMPTY_SVAL(options.meteorAppSettings);
	result["min_processes"] = VAL(options.minProcesses, 1u);
	result["max_processes"] = VAL(options.maxProcesses, 0u);
	result["environment"] = SVAL(options.environment); // TODO: default value depends on integration mode
	result["spawn_method"] = SVAL(options.spawnMethod, DEFAULT_SPAWN_METHOD);
	result["bind_address"] = SVAL(options.bindAddress, DEFAULT_BIND_ADDRESS);
	result["start_timeout"] = VAL(options.startTimeout / 1000.0, DEFAULT_START_TIMEOUT / 1000.0);
	result["max_preloader_idle_time"] = VAL((Json::UInt) options.maxPreloaderIdleTime,
		(Json::UInt) DEFAULT_MAX_PRELOADER_IDLE_TIME);
	result["max_out_of_band_work_instances"] = VAL(options.maxOutOfBandWorkInstances,
		(Json::UInt) 1);
	result["base_uri"] = SVAL(options.baseURI, P_STATIC_STRING("/"));
	result["user"] = SVAL(options.user, options.defaultUser);
	result["group"] = SVAL(options.group, options.defaultGroup);
	result["user_switching"] = VAL(options.userSwitching); // TODO: default value depends on integration mode and euid
	result["file_descriptor_ulimit"] = VAL(options.fileDescriptorUlimit, 0u);
	result["load_shell_envvars"] = VAL(options.loadShellEnvvars); // TODO: default value depends on integration mode
	result["preload_bundler"] = VAL(options.preloadBundler);
	result["max_request_queue_size"] = VAL(options.maxRequestQueueSize,
		(Json::UInt) DEFAULT_MAX_REQUEST_QUEUE_SIZE);
	result["max_requests"] = VAL((Json::UInt) options.maxRequests, 0u);
	result["abort_websockets_on_process_shutdown"] = VAL(options.abortWebsocketsOnProcessShutdown);
	result["force_max_concurrent_requests_per_process"] = VAL(options.forceMaxConcurrentRequestsPerProcess, -1);
	result["restart_dir"] = NON_EMPTY_SVAL(options.restartDir);
	result["sticky_sessions_cookie_attributes"] = SVAL(options.stickySessionsCookieAttributes, DEFAULT_STICKY_SESSIONS_COOKIE_ATTRIBUTES);

	if (!options.environmentVariables.empty()) {
		DynamicBuffer envvarsData(options.environmentVariables.size() * 3 / 4);
		size_t envvarsDataSize = modp_b64_decode(envvarsData.data,
			options.environmentVariables.data(), options.environmentVariables.size());
		if (envvarsDataSize == (size_t) -1) {
			P_WARN("Unable to decode environment variable data");
		} else {
			Json::Value envvars(Json::objectValue);
			vector<string> envvarsAry;
			unsigned int i;

			split(StaticString(envvarsData.data, envvarsDataSize), '\0', envvarsAry);
			if (!envvarsAry.empty() && envvarsAry.back().empty()) {
				envvarsAry.pop_back();
			}
			assert(envvars.size() % 2 == 0);

			for (i = 0; i < envvarsAry.size(); i += 2) {
				envvars[envvarsAry[i]] = envvarsAry[i + 1];
			}

			result["environment_variables"] = VAL(envvars, Json::objectValue);
		}
	} else {
		result["environment_variables"] = VAL(Json::objectValue, Json::objectValue);
	}

	// Missing: sticky_sessions, sticky_session_cookie_name, friendly_error_pages, custom_error_page

	/******************/

	#undef VAL
	#undef SVAL
	#undef NON_EMPTY_SVAL
}


} // namespace ApplicationPool2
} // namespace Passenger

#endif // _PASSENGER_APPLICATION_POOL_GROUP_STATE_INSPECTION_CPP_

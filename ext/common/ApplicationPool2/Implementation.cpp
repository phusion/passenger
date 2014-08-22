/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2014 Phusion
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
#include <typeinfo>
#include <algorithm>
#include <utility>
#include <cstdio>
#include <sstream>
#include <limits.h>
#include <unistd.h>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>
#include <boost/cstdint.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <oxt/backtrace.hpp>
#include <ApplicationPool2/Pool.h>
#include <ApplicationPool2/SuperGroup.h>
#include <ApplicationPool2/Group.h>
#include <ApplicationPool2/PipeWatcher.h>
#include <ApplicationPool2/ErrorRenderer.h>
#include <Exceptions.h>
#include <Hooks.h>
#include <MessageReadersWriters.h>
#include <Utils.h>
#include <Utils/IOUtils.h>
#include <Utils/ScopeGuard.h>
#include <Utils/MessageIO.h>
#include <Utils/JsonUtils.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;

#define TRY_COPY_EXCEPTION(klass) \
	do { \
		const klass *ep = dynamic_cast<const klass *>(&e); \
		if (ep != NULL) { \
			return boost::make_shared<klass>(*ep); \
		} \
	} while (false)

ExceptionPtr
copyException(const tracable_exception &e) {
	TRY_COPY_EXCEPTION(FileSystemException);
	TRY_COPY_EXCEPTION(TimeRetrievalException);
	TRY_COPY_EXCEPTION(SystemException);

	TRY_COPY_EXCEPTION(FileNotFoundException);
	TRY_COPY_EXCEPTION(EOFException);
	TRY_COPY_EXCEPTION(IOException);

	TRY_COPY_EXCEPTION(ConfigurationException);

	TRY_COPY_EXCEPTION(RequestQueueFullException);
	TRY_COPY_EXCEPTION(GetAbortedException);
	TRY_COPY_EXCEPTION(SpawnException);

	TRY_COPY_EXCEPTION(InvalidModeStringException);
	TRY_COPY_EXCEPTION(ArgumentException);

	TRY_COPY_EXCEPTION(RuntimeException);

	TRY_COPY_EXCEPTION(TimeoutException);

	TRY_COPY_EXCEPTION(NonExistentUserException);
	TRY_COPY_EXCEPTION(NonExistentGroupException);
	TRY_COPY_EXCEPTION(SecurityException);

	TRY_COPY_EXCEPTION(SyntaxError);

	TRY_COPY_EXCEPTION(boost::thread_interrupted);

	return boost::make_shared<tracable_exception>(e);
}

#define TRY_RETHROW_EXCEPTION(klass) \
	do { \
		const klass *ep = dynamic_cast<const klass *>(&*e); \
		if (ep != NULL) { \
			throw klass(*ep); \
		} \
	} while (false)

void
rethrowException(const ExceptionPtr &e) {
	TRY_RETHROW_EXCEPTION(FileSystemException);
	TRY_RETHROW_EXCEPTION(TimeRetrievalException);
	TRY_RETHROW_EXCEPTION(SystemException);

	TRY_RETHROW_EXCEPTION(FileNotFoundException);
	TRY_RETHROW_EXCEPTION(EOFException);
	TRY_RETHROW_EXCEPTION(IOException);

	TRY_RETHROW_EXCEPTION(ConfigurationException);

	TRY_RETHROW_EXCEPTION(SpawnException);
	TRY_RETHROW_EXCEPTION(RequestQueueFullException);
	TRY_RETHROW_EXCEPTION(GetAbortedException);

	TRY_RETHROW_EXCEPTION(InvalidModeStringException);
	TRY_RETHROW_EXCEPTION(ArgumentException);

	TRY_RETHROW_EXCEPTION(RuntimeException);

	TRY_RETHROW_EXCEPTION(TimeoutException);

	TRY_RETHROW_EXCEPTION(NonExistentUserException);
	TRY_RETHROW_EXCEPTION(NonExistentGroupException);
	TRY_RETHROW_EXCEPTION(SecurityException);

	TRY_RETHROW_EXCEPTION(SyntaxError);

	TRY_RETHROW_EXCEPTION(boost::lock_error);
	TRY_RETHROW_EXCEPTION(boost::thread_resource_error);
	TRY_RETHROW_EXCEPTION(boost::unsupported_thread_option);
	TRY_RETHROW_EXCEPTION(boost::invalid_thread_argument);
	TRY_RETHROW_EXCEPTION(boost::thread_permission_error);

	TRY_RETHROW_EXCEPTION(boost::thread_interrupted);
	TRY_RETHROW_EXCEPTION(boost::thread_exception);
	TRY_RETHROW_EXCEPTION(boost::condition_error);

	throw tracable_exception(*e);
}

void processAndLogNewSpawnException(SpawnException &e, const Options &options,
	const SpawnerConfigPtr &config)
{
	TRACE_POINT();
	UnionStation::TransactionPtr transaction;
	ErrorRenderer renderer(config->resourceLocator);
	string appMessage = e.getErrorPage();
	string errorId;
	char filename[PATH_MAX];
	stringstream stream;

	if (options.analytics && config->unionStationCore != NULL) {
		try {
			UPDATE_TRACE_POINT();
			transaction = config->unionStationCore->newTransaction(
				options.getAppGroupName(),
				"exceptions",
				options.unionStationKey);
			errorId = transaction->getTxnId();
		} catch (const tracable_exception &e2) {
			transaction.reset();
			P_WARN("Cannot log to Union Station: " << e2.what() <<
				"\n  Backtrace:\n" << e2.backtrace());
		}
	}

	UPDATE_TRACE_POINT();
	if (appMessage.empty()) {
		appMessage = "none";
	}
	if (errorId.empty()) {
		errorId = config->randomGenerator->generateHexString(4);
	}
	e.set("error_id", errorId);

	try {
		int fd = -1;
		FdGuard guard(fd, true);
		string errorPage;

		UPDATE_TRACE_POINT();
		errorPage = renderer.renderWithDetails(appMessage, options, &e);

		#if (defined(__linux__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 11))) || defined(__APPLE__) || defined(__FreeBSD__)
			snprintf(filename, PATH_MAX, "%s/passenger-error-XXXXXX.html",
				getSystemTempDir());
			fd = mkstemps(filename, sizeof(".html") - 1);
		#else
			snprintf(filename, PATH_MAX, "%s/passenger-error.XXXXXX",
				getSystemTempDir());
			fd = mkstemp(filename);
		#endif
		if (fd == -1) {
			int e = errno;
			throw SystemException("Cannot generate a temporary filename",
				e);
		}

		UPDATE_TRACE_POINT();
		writeExact(fd, errorPage);
	} catch (const SystemException &e2) {
		filename[0] = '\0';
		P_ERROR("Cannot render an error page: " << e2.what() << "\n" <<
			e2.backtrace());
	}

	if (transaction != NULL) {
		try {
			UPDATE_TRACE_POINT();
			transaction->message("Context: spawning");
			transaction->message("Message: " +
				jsonString(e.what()));
			transaction->message("App message: " +
				jsonString(appMessage));

			const char *kind;
			switch (e.getErrorKind()) {
			case SpawnException::PRELOADER_STARTUP_ERROR:
				kind = "PRELOADER_STARTUP_ERROR";
				break;
			case SpawnException::PRELOADER_STARTUP_PROTOCOL_ERROR:
				kind = "PRELOADER_STARTUP_PROTOCOL_ERROR";
				break;
			case SpawnException::PRELOADER_STARTUP_TIMEOUT:
				kind = "PRELOADER_STARTUP_TIMEOUT";
				break;
			case SpawnException::PRELOADER_STARTUP_EXPLAINABLE_ERROR:
				kind = "PRELOADER_STARTUP_EXPLAINABLE_ERROR";
				break;
			case SpawnException::APP_STARTUP_ERROR:
				kind = "APP_STARTUP_ERROR";
				break;
			case SpawnException::APP_STARTUP_PROTOCOL_ERROR:
				kind = "APP_STARTUP_PROTOCOL_ERROR";
				break;
			case SpawnException::APP_STARTUP_TIMEOUT:
				kind = "APP_STARTUP_TIMEOUT";
				break;
			case SpawnException::APP_STARTUP_EXPLAINABLE_ERROR:
				kind = "APP_STARTUP_EXPLAINABLE_ERROR";
				break;
			default:
				kind = "UNDEFINED_ERROR";
				break;
			}
			transaction->message(string("Kind: ") + kind);

			Json::Value details;
			const map<string, string> &annotations = e.getAnnotations();
			map<string, string>::const_iterator it, end = annotations.end();

			for (it = annotations.begin(); it != end; it++) {
				details[it->first] = it->second;
			}

			// This information is not very useful. Union Station
			// already collects system metrics.
			details.removeMember("system_metrics");
			// Don't include environment variables because they may
			// contain sensitive information.
			details.removeMember("envvars");

			transaction->message("Details: " + stringifyJson(details));
		} catch (const tracable_exception &e2) {
			P_WARN("Cannot log to Union Station: " << e2.what() <<
				"\n  Backtrace:\n" << e2.backtrace());
		}
	}

	UPDATE_TRACE_POINT();
	stream << "Could not spawn process for application " << options.appRoot <<
		": " << e.what() << "\n" <<
		"  Error ID: " << errorId << "\n";
	if (filename[0] != '\0') {
		stream << "  Error details saved to: " << filename << "\n";
	}
	stream << "  Message from application: " << appMessage << "\n";
	P_ERROR(stream.str());

	if (config->agentsOptions != NULL) {
		HookScriptOptions hOptions;
		hOptions.name = "spawn_failed";
		hOptions.spec = config->agentsOptions->get("hook_spawn_failed", false);
		hOptions.agentsOptions = config->agentsOptions;
		hOptions.environment.push_back(make_pair("PASSENGER_APP_ROOT", options.appRoot));
		hOptions.environment.push_back(make_pair("PASSENGER_APP_GROUP_NAME", options.getAppGroupName()));
		hOptions.environment.push_back(make_pair("PASSENGER_ERROR_MESSAGE", e.what()));
		hOptions.environment.push_back(make_pair("PASSENGER_ERROR_ID", errorId));
		hOptions.environment.push_back(make_pair("PASSENGER_APP_ERROR_MESSAGE", appMessage));
		oxt::thread(boost::bind(runHookScripts, hOptions),
			"Hook: spawn_failed", 256 * 1024);
	}
}


const SuperGroupPtr
Pool::getSuperGroup(const char *name) {
	return superGroups.get(name);
}


boost::mutex &
SuperGroup::getPoolSyncher(const PoolPtr &pool) {
	return pool->syncher;
}

void
SuperGroup::runAllActions(const vector<Callback> &actions) {
	Pool::runAllActions(actions);
}

string
SuperGroup::generateSecret() const {
	return getPool()->getRandomGenerator()->generateAsciiString(43);
}

void
SuperGroup::runInitializationHooks() const {
	getPool()->runHookScripts("after_initialize_supergroup",
		boost::bind(&SuperGroup::setupInitializationOrDestructionHook, this, _1));
}

void
SuperGroup::runDestructionHooks() const {
	getPool()->runHookScripts("before_destroy_supergroup",
		boost::bind(&SuperGroup::setupInitializationOrDestructionHook, this, _1));
}

void
SuperGroup::setupInitializationOrDestructionHook(HookScriptOptions &options) const {
	options.environment.push_back(make_pair("PASSENGER_APP_ROOT", this->options.appRoot));
}

void
SuperGroup::createInterruptableThread(const boost::function<void ()> &func, const string &name,
	unsigned int stackSize)
{
	getPool()->interruptableThreads.create_thread(func, name, stackSize);
}

void
SuperGroup::realDoInitialize(const Options &options, unsigned int generation) {
	vector<ComponentInfo> componentInfos;
	vector<ComponentInfo>::const_iterator it;
	ExceptionPtr exception;

	PoolPtr pool = getPool();

	P_TRACE(2, "Initializing SuperGroup " << inspect() << " in the background...");
	try {
		componentInfos = loadComponentInfos(options);
	} catch (const tracable_exception &e) {
		exception = copyException(e);
	}
	if (componentInfos.empty() && exception == NULL) {
		string message = "The directory " +
			options.appRoot +
			" does not seem to contain a web application.";
		boost::shared_ptr<SpawnException> spawnException =
			boost::make_shared<SpawnException>(
				message, message, false);
		exception = spawnException;
		processAndLogNewSpawnException(*spawnException, options,
			pool->getSpawnerConfig());
	}

	Pool::DebugSupportPtr debug = pool->debugSupport;
	vector<Callback> actions;
	{
		if (debug != NULL && debug->superGroup) {
			debug->debugger->send("About to finish SuperGroup initialization");
			debug->messages->recv("Proceed with initializing SuperGroup");
		}

		boost::unique_lock<boost::mutex> lock(getPoolSyncher(pool));
		this_thread::disable_interruption di;
		this_thread::disable_syscall_interruption dsi;
		NOT_EXPECTING_EXCEPTIONS();
		if (OXT_UNLIKELY(getPool() == NULL || generation != this->generation)) {
			return;
		}
		P_TRACE(2, "Initialization of SuperGroup " << inspect() << " almost done; grabbed lock");
		assert(state == INITIALIZING);
		verifyInvariants();

		if (componentInfos.empty()) {
			/* Somehow initialization failed. Maybe something has deleted
			 * the supergroup files while we're working.
			 */
			assert(exception != NULL);
			setState(DESTROYED);

			actions.reserve(getWaitlist.size());
			while (!getWaitlist.empty()) {
				const GetWaiter &waiter = getWaitlist.front();
				actions.push_back(boost::bind(waiter.callback,
					SessionPtr(), exception));
				getWaitlist.pop_front();
			}
		} else {
			for (it = componentInfos.begin(); it != componentInfos.end(); it++) {
				const ComponentInfo &info = *it;
				GroupPtr group = boost::make_shared<Group>(shared_from_this(),
					options, info);
				groups.push_back(group);
				if (info.isDefault) {
					defaultGroup = group.get();
				}
			}

			setState(READY);
			assignGetWaitlistToGroups(actions);
		}

		verifyInvariants();
		P_TRACE(2, "Done initializing SuperGroup " << inspect());
	}

	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;
	runAllActions(actions);
	runInitializationHooks();
}

void
SuperGroup::realDoRestart(const Options &options, unsigned int generation) {
	TRACE_POINT();
	vector<ComponentInfo> componentInfos = loadComponentInfos(options);
	vector<ComponentInfo>::const_iterator it;

	PoolPtr pool = getPool();
	Pool::DebugSupportPtr debug = pool->debugSupport;
	if (debug != NULL && debug->superGroup) {
		debug->debugger->send("About to finish SuperGroup restart");
		debug->messages->recv("Proceed with restarting SuperGroup");
	}

	boost::unique_lock<boost::mutex> lock(getPoolSyncher(pool));
	if (OXT_UNLIKELY(this->generation != generation)) {
		return;
	}

	assert(state == RESTARTING);
	verifyInvariants();

	vector<GroupPtr> allGroups;
	vector<GroupPtr> updatedGroups;
	vector<GroupPtr> newGroups;
	vector<GroupPtr>::const_iterator g_it;
	vector<Callback> actions;
	this->options = options;

	// Update the component information for existing groups.
	UPDATE_TRACE_POINT();
	for (it = componentInfos.begin(); it != componentInfos.end(); it++) {
		const ComponentInfo &info = *it;
		pair<GroupPtr, unsigned int> result =
			findGroupCorrespondingToComponent(groups, info);
		GroupPtr group = result.first;
		if (group != NULL) {
			unsigned int index = result.second;
			group->componentInfo = info;
			updatedGroups.push_back(group);
			groups[index].reset();
		} else {
			// This is not an existing group but a new one,
			// so create it.
			group = boost::make_shared<Group>(shared_from_this(),
				options, info);
			newGroups.push_back(group);
		}
		// allGroups must be in the same order as componentInfos.
		allGroups.push_back(group);
	}

	// Some components might have been deleted, so delete the
	// corresponding groups.
	detachAllGroups(groups, actions);

	// Tell all previous existing groups to restart.
	for (g_it = updatedGroups.begin(); g_it != updatedGroups.end(); g_it++) {
		GroupPtr group = *g_it;
		group->restart(options);
	}

	groups = allGroups;
	defaultGroup = findDefaultGroup(allGroups);
	setState(READY);
	assignGetWaitlistToGroups(actions);

	UPDATE_TRACE_POINT();
	verifyInvariants();
	lock.unlock();
	runAllActions(actions);
}


Group::Group(const SuperGroupPtr &_superGroup, const Options &options, const ComponentInfo &info)
	: superGroup(_superGroup),
	  name(_superGroup->name + "#" + info.name),
	  secret(generateSecret(_superGroup)),
	  uuid(generateUuid(_superGroup)),
	  componentInfo(info)
{
	enabledCount   = 0;
	disablingCount = 0;
	disabledCount  = 0;
	spawner        = getPool()->spawnerFactory->create(options);
	restartsInitiated = 0;
	processesBeingSpawned = 0;
	m_spawning     = false;
	m_restarting   = false;
	lifeStatus     = ALIVE;
	if (options.restartDir.empty()) {
		restartFile = options.appRoot + "/tmp/restart.txt";
		alwaysRestartFile = options.appRoot + "/tmp/always_restart.txt";
	} else if (options.restartDir[0] == '/') {
		restartFile = options.restartDir + "/restart.txt";
		alwaysRestartFile = options.restartDir + "/always_restart.txt";
	} else {
		restartFile = options.appRoot + "/" + options.restartDir + "/restart.txt";
		alwaysRestartFile = options.appRoot + "/" + options.restartDir + "/always_restart.txt";
	}
	resetOptions(options);

	detachedProcessesCheckerActive = false;
}

Group::~Group() {
	LifeStatus lifeStatus = getLifeStatus();
	if (OXT_UNLIKELY(lifeStatus == ALIVE)) {
		P_BUG("You must call Group::shutdown() before destroying a Group.");
	}
	assert(lifeStatus == SHUT_DOWN);
	assert(!detachedProcessesCheckerActive);
}

PoolPtr
Group::getPool() const {
	return getSuperGroup()->getPool();
}

void
Group::onSessionInitiateFailure(const ProcessPtr &process, Session *session) {
	vector<Callback> actions;

	TRACE_POINT();
	// Standard resource management boilerplate stuff...
	PoolPtr pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	assert(process->isAlive());
	assert(isAlive() || getLifeStatus() == SHUTTING_DOWN);

	UPDATE_TRACE_POINT();
	P_DEBUG("Could not initiate a session with process " <<
		process->inspect() << ", detaching from pool if possible");
	if (!pool->detachProcessUnlocked(process, actions)) {
		P_DEBUG("Process was already detached");
	}
	pool->fullVerifyInvariants();
	lock.unlock();
	runAllActions(actions);
}

void
Group::onSessionClose(const ProcessPtr &process, Session *session) {
	TRACE_POINT();
	// Standard resource management boilerplate stuff...
	PoolPtr pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	assert(process->isAlive());
	assert(isAlive() || getLifeStatus() == SHUTTING_DOWN);

	P_TRACE(2, "Session closed for process " << process->inspect());
	verifyInvariants();
	UPDATE_TRACE_POINT();

	/* Update statistics. */
	process->sessionClosed(session);
	assert(process->getLifeStatus() == Process::ALIVE);
	assert(process->enabled == Process::ENABLED
		|| process->enabled == Process::DISABLING
		|| process->enabled == Process::DETACHED);
	if (process->enabled == Process::ENABLED) {
		pqueue.decrease(process->pqHandle, process->busyness());
	}

	/* This group now has a process that's guaranteed to be not
	 * totally busy.
	 */
	assert(!process->isTotallyBusy());

	bool detachingBecauseOfMaxRequests = false;
	bool detachingBecauseCapacityNeeded = false;
	bool shouldDetach =
		( detachingBecauseOfMaxRequests = (
			options.maxRequests > 0
			&& process->processed >= options.maxRequests
		)) || (
			detachingBecauseCapacityNeeded = (
				process->sessions == 0
				&& getWaitlist.empty()
				&& (
					!pool->getWaitlist.empty()
					|| anotherGroupIsWaitingForCapacity()
				)
			)
		);
	bool shouldDisable =
		process->enabled == Process::DISABLING
		&& process->sessions == 0
		&& enabledCount > 0;

	if (shouldDetach || shouldDisable) {
		UPDATE_TRACE_POINT();
		vector<Callback> actions;

		if (shouldDetach) {
			if (detachingBecauseCapacityNeeded) {
				/* Someone might be trying to get() a session for a different
				 * group that couldn't be spawned because of lack of pool capacity.
				 * If this group isn't under sufficiently load (as apparent by the
				 * checked conditions) then now's a good time to detach
				 * this process or group in order to free capacity.
				 */
				P_DEBUG("Process " << process->inspect() << " is no longer totally "
					"busy; detaching it in order to make room in the pool");
			} else {
				/* This process has processed its maximum number of requests,
				 * so we detach it.
				 */
				P_DEBUG("Process " << process->inspect() <<
					" has reached its maximum number of requests (" <<
					options.maxRequests << "); detaching it");
			}
			pool->detachProcessUnlocked(process, actions);
		} else {
			removeProcessFromList(process, disablingProcesses);
			addProcessToList(process, disabledProcesses);
			removeFromDisableWaitlist(process, DR_SUCCESS, actions);
			maybeInitiateOobw(process);
		}

		pool->fullVerifyInvariants();
		lock.unlock();
		runAllActions(actions);

	} else {
		UPDATE_TRACE_POINT();

		// This could change process->enabled.
		maybeInitiateOobw(process);

		if (!getWaitlist.empty() && process->enabled == Process::ENABLED) {
			/* If there are clients on this group waiting for a process to
			 * become available then call them now.
			 */
			UPDATE_TRACE_POINT();
			// Already calls verifyInvariants().
			assignSessionsToGetWaitersQuickly(lock);
		}
	}
}

void
Group::requestOOBW(const ProcessPtr &process) {
	// Standard resource management boilerplate stuff...
	PoolPtr pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	if (isAlive() && process->isAlive() && process->oobwStatus == Process::OOBW_NOT_ACTIVE) {
		process->oobwStatus = Process::OOBW_REQUESTED;
	}
}

bool
Group::oobwAllowed() const {
	unsigned int oobwInstances = 0;
	foreach (const ProcessPtr &process, disablingProcesses) {
		if (process->oobwStatus == Process::OOBW_IN_PROGRESS) {
			oobwInstances += 1;
		}
	}
	foreach (const ProcessPtr &process, disabledProcesses) {
		if (process->oobwStatus == Process::OOBW_IN_PROGRESS) {
			oobwInstances += 1;
		}
	}
	return oobwInstances < options.maxOutOfBandWorkInstances;
}

bool
Group::shouldInitiateOobw(const ProcessPtr &process) const {
	return process->oobwStatus == Process::OOBW_REQUESTED
		&& process->enabled != Process::DETACHED
		&& process->isAlive()
		&& oobwAllowed();
}

void
Group::maybeInitiateOobw(const ProcessPtr &process) {
	if (shouldInitiateOobw(process)) {
		initiateOobw(process);
	}
}

// The 'self' parameter is for keeping the current Group object alive
void
Group::lockAndMaybeInitiateOobw(const ProcessPtr &process, DisableResult result, GroupPtr self) {
	TRACE_POINT();

	// Standard resource management boilerplate stuff...
	PoolPtr pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	if (OXT_UNLIKELY(!process->isAlive() || !isAlive())) {
		return;
	}

	assert(process->oobwStatus == Process::OOBW_IN_PROGRESS);

	if (result == DR_SUCCESS) {
		if (process->enabled == Process::DISABLED) {
			P_DEBUG("Process " << process->inspect() << " disabled; proceeding " <<
				"with out-of-band work");
			process->oobwStatus = Process::OOBW_REQUESTED;
			if (shouldInitiateOobw(process)) {
				initiateOobw(process);
			} else {
				// We do not re-enable the process because it's likely that the
				// administrator has explicitly changed the state.
				P_DEBUG("Out-of-band work for process " << process->inspect() << " aborted "
					"because the process no longer requests out-of-band work");
				process->oobwStatus = Process::OOBW_NOT_ACTIVE;
			}
		} else {
			// We do not re-enable the process because it's likely that the
			// administrator has explicitly changed the state.
			P_DEBUG("Out-of-band work for process " << process->inspect() << " aborted "
				"because the process was reenabled after disabling");
			process->oobwStatus = Process::OOBW_NOT_ACTIVE;
		}
	} else {
		P_DEBUG("Out-of-band work for process " << process->inspect() << " aborted "
			"because the process could not be disabled");
		process->oobwStatus = Process::OOBW_NOT_ACTIVE;
	}
}

void
Group::initiateOobw(const ProcessPtr &process) {
	assert(process->oobwStatus == Process::OOBW_REQUESTED);

	process->oobwStatus = Process::OOBW_IN_PROGRESS;

	if (process->enabled == Process::ENABLED
	 || process->enabled == Process::DISABLING)
	{
		// We want the process to be disabled. However, disabling a process is potentially
		// asynchronous, so we pass a callback which will re-aquire the lock and call this
		// method again.
		P_DEBUG("Disabling process " << process->inspect() << " in preparation for OOBW");
		DisableResult result = disable(process,
			boost::bind(&Group::lockAndMaybeInitiateOobw, this,
				_1, _2, shared_from_this()));
		switch (result) {
		case DR_SUCCESS:
			// Continue code flow.
			break;
		case DR_DEFERRED:
			// lockAndMaybeInitiateOobw() will eventually be called.
			return;
		case DR_ERROR:
		case DR_NOOP:
			P_DEBUG("Out-of-band work for process " << process->inspect() << " aborted "
				"because the process could not be disabled");
			process->oobwStatus = Process::OOBW_NOT_ACTIVE;
			return;
		default:
			P_BUG("Unexpected disable() result " << result);
		}
	}

	assert(process->enabled == Process::DISABLED);
	assert(process->sessions == 0);

	P_DEBUG("Initiating OOBW request for process " << process->inspect());
	interruptableThreads.create_thread(
		boost::bind(&Group::spawnThreadOOBWRequest, this, shared_from_this(), process),
		"OOBW request thread for process " + process->inspect(),
		POOL_HELPER_THREAD_STACK_SIZE);
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::spawnThreadOOBWRequest(GroupPtr self, ProcessPtr process) {
	TRACE_POINT();
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;

	Socket *socket;
	Connection connection;
	PoolPtr pool = getPool();
	Pool::DebugSupportPtr debug = pool->debugSupport;

	UPDATE_TRACE_POINT();
	P_DEBUG("Performing OOBW request for process " << process->inspect());
	if (debug != NULL && debug->oobw) {
		debug->debugger->send("OOBW request about to start");
		debug->messages->recv("Proceed with OOBW request");
	}

	UPDATE_TRACE_POINT();
	{
		// Standard resource management boilerplate stuff...
		boost::unique_lock<boost::mutex> lock(pool->syncher);
		if (OXT_UNLIKELY(!process->isAlive()
			|| process->enabled == Process::DETACHED
			|| !isAlive()))
		{
			return;
		}

		if (process->enabled != Process::DISABLED) {
			UPDATE_TRACE_POINT();
			P_INFO("Out-of-Band Work canceled: process " << process->inspect() <<
				" was concurrently re-enabled.");
			if (debug != NULL && debug->oobw) {
				debug->debugger->send("OOBW request canceled");
			}
			return;
		}

		assert(process->oobwStatus == Process::OOBW_IN_PROGRESS);
		assert(process->sessions == 0);
		socket = process->sessionSockets.top();
		assert(socket != NULL);
	}

	UPDATE_TRACE_POINT();
	unsigned long long timeout = 1000 * 1000 * 60; // 1 min
	try {
		this_thread::restore_interruption ri(di);
		this_thread::restore_syscall_interruption rsi(dsi);

		// Grab a connection. The connection is marked as fail in order to
		// ensure it is closed / recycled after this request (otherwise we'd
		// need to completely read the response).
		connection = socket->checkoutConnection();
		connection.fail = true;
		ScopeGuard guard(boost::bind(&Socket::checkinConnection, socket, connection));

		// This is copied from RequestHandler when it is sending data using the
		// "session" protocol.
		char sizeField[sizeof(boost::uint32_t)];
		SmallVector<StaticString, 10> data;

		data.push_back(StaticString(sizeField, sizeof(boost::uint32_t)));
		data.push_back(makeStaticStringWithNull("REQUEST_METHOD"));
		data.push_back(makeStaticStringWithNull("OOBW"));

		data.push_back(makeStaticStringWithNull("PASSENGER_CONNECT_PASSWORD"));
		data.push_back(makeStaticStringWithNull(process->connectPassword));

		boost::uint32_t dataSize = 0;
		for (unsigned int i = 1; i < data.size(); i++) {
			dataSize += (boost::uint32_t) data[i].size();
		}
		Uint32Message::generate(sizeField, dataSize);

		gatheredWrite(connection.fd, &data[0], data.size(), &timeout);

		// We do not care what the actual response is ... just wait for it.
		UPDATE_TRACE_POINT();
		waitUntilReadable(connection.fd, &timeout);
	} catch (const SystemException &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
	} catch (const TimeoutException &e) {
		P_ERROR("*** ERROR: " << e.what() << "\n" << e.backtrace());
	}

	UPDATE_TRACE_POINT();
	vector<Callback> actions;
	{
		// Standard resource management boilerplate stuff...
		PoolPtr pool = getPool();
		boost::unique_lock<boost::mutex> lock(pool->syncher);
		if (OXT_UNLIKELY(!process->isAlive() || !isAlive())) {
			return;
		}

		process->oobwStatus = Process::OOBW_NOT_ACTIVE;
		if (process->enabled == Process::DISABLED) {
			enable(process, actions);
			assignSessionsToGetWaiters(actions);
		}

		pool->fullVerifyInvariants();

		initiateNextOobwRequest();
	}
	UPDATE_TRACE_POINT();
	runAllActions(actions);
	actions.clear();

	UPDATE_TRACE_POINT();
	P_DEBUG("Finished OOBW request for process " << process->inspect());
	if (debug != NULL && debug->oobw) {
		debug->debugger->send("OOBW request finished");
	}
}

void
Group::initiateNextOobwRequest() {
	ProcessList::const_iterator it, end = enabledProcesses.end();
	for (it = enabledProcesses.begin(); it != end; it++) {
		const ProcessPtr &process = *it;
		if (shouldInitiateOobw(process)) {
			// We keep an extra reference to processes to prevent premature destruction.
			ProcessPtr p = process;
			initiateOobw(p);
			return;
		}
	}
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::spawnThreadMain(GroupPtr self, SpawnerPtr spawner, Options options, unsigned int restartsInitiated) {
	spawnThreadRealMain(spawner, options, restartsInitiated);
}

void
Group::spawnThreadRealMain(const SpawnerPtr &spawner, const Options &options, unsigned int restartsInitiated) {
	TRACE_POINT();
	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;

	PoolPtr pool = getPool();
	Pool::DebugSupportPtr debug = pool->debugSupport;

	bool done = false;
	while (!done) {
		bool shouldFail = false;
		if (debug != NULL && debug->spawning) {
			UPDATE_TRACE_POINT();
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			this_thread::interruption_point();
			string iteration;
			{
				LockGuard g(debug->syncher);
				debug->spawnLoopIteration++;
				iteration = toString(debug->spawnLoopIteration);
			}
			P_DEBUG("Begin spawn loop iteration " << iteration);
			debug->debugger->send("Begin spawn loop iteration " +
				iteration);

			vector<string> cases;
			cases.push_back("Proceed with spawn loop iteration " + iteration);
			cases.push_back("Fail spawn loop iteration " + iteration);
			MessagePtr message = debug->messages->recvAny(cases);
			shouldFail = message->name == "Fail spawn loop iteration " + iteration;
		}

		ProcessPtr process;
		ExceptionPtr exception;
		try {
			UPDATE_TRACE_POINT();
			this_thread::restore_interruption ri(di);
			this_thread::restore_syscall_interruption rsi(dsi);
			if (shouldFail) {
				SpawnException e("Simulated failure");
				processAndLogNewSpawnException(e, options, pool->getSpawnerConfig());
				throw e;
			} else {
				process = spawner->spawn(options);
				process->setGroup(shared_from_this());
			}
		} catch (const thread_interrupted &) {
			break;
		} catch (const tracable_exception &e) {
			exception = copyException(e);
			// Let other (unexpected) exceptions crash the program so
			// gdb can generate a backtrace.
		}

		UPDATE_TRACE_POINT();
		ScopeGuard guard(boost::bind(Process::forceTriggerShutdownAndCleanup, process));
		boost::unique_lock<boost::mutex> lock(pool->syncher);

		if (!isAlive()) {
			if (process != NULL) {
				P_DEBUG("Group is being shut down so dropping process " <<
					process->inspect() << " which we just spawned and exiting spawn loop");
			} else {
				P_DEBUG("The group is being shut down. A process failed "
					"to be spawned anyway, so ignoring this error and exiting "
					"spawn loop");
			}
			// We stop immediately because any previously assumed invariants
			// may have been violated.
			break;
		} else if (restartsInitiated != this->restartsInitiated) {
			if (process != NULL) {
				P_DEBUG("A restart was issued for the group, so dropping process " <<
					process->inspect() << " which we just spawned and exiting spawn loop");
			} else {
				P_DEBUG("A restart was issued for the group. A process failed "
					"to be spawned anyway, so ignoring this error and exiting "
					"spawn loop");
			}
			// We stop immediately because any previously assumed invariants
			// may have been violated.
			break;
		}

		verifyInvariants();
		assert(m_spawning);
		assert(processesBeingSpawned > 0);

		processesBeingSpawned--;
		assert(processesBeingSpawned == 0);

		UPDATE_TRACE_POINT();
		vector<Callback> actions;
		if (process != NULL) {
			AttachResult result = attach(process, actions);
			if (result == AR_OK) {
				guard.clear();
				if (getWaitlist.empty()) {
					pool->assignSessionsToGetWaiters(actions);
				} else {
					assignSessionsToGetWaiters(actions);
				}
				P_DEBUG("New process count = " << enabledCount <<
					", remaining get waiters = " << getWaitlist.size());
			} else {
				done = true;
				P_DEBUG("Unable to attach spawned process " << process->inspect());
				if (result == AR_ANOTHER_GROUP_IS_WAITING_FOR_CAPACITY) {
					pool->possiblySpawnMoreProcessesForExistingGroups();
				}
			}
		} else {
			// TODO: sure this is the best thing? if there are
			// processes currently alive we should just use them.
			if (enabledCount == 0) {
				enableAllDisablingProcesses(actions);
			}
			Pool::assignExceptionToGetWaiters(getWaitlist, exception, actions);
			pool->assignSessionsToGetWaiters(actions);
			done = true;
		}

		done = done
			|| (processLowerLimitsSatisfied() && getWaitlist.empty())
			|| processUpperLimitsReached()
			|| pool->atFullCapacity(false);
		m_spawning = !done;
		if (done) {
			P_DEBUG("Spawn loop done");
		} else {
			processesBeingSpawned++;
			P_DEBUG("Continue spawning");
		}

		UPDATE_TRACE_POINT();
		pool->fullVerifyInvariants();
		lock.unlock();
		UPDATE_TRACE_POINT();
		runAllActions(actions);
		UPDATE_TRACE_POINT();
	}

	if (debug != NULL && debug->spawning) {
		debug->debugger->send("Spawn loop done");
	}
}

bool
Group::shouldSpawn() const {
	return allowSpawn()
		&& (
			!processLowerLimitsSatisfied()
			|| allEnabledProcessesAreTotallyBusy()
			|| !getWaitlist.empty()
		);
}

bool
Group::shouldSpawnForGetAction() const {
	return enabledCount == 0 || shouldSpawn();
}

void
Group::restart(const Options &options, RestartMethod method) {
	vector<Callback> actions;

	assert(isAlive());
	P_DEBUG("Restarting group " << name);

	// If there is currently a restarter thread or a spawner thread active,
	// the following tells them to abort their current work as soon as possible.
	restartsInitiated++;

	processesBeingSpawned = 0;
	m_spawning   = false;
	m_restarting = true;
	uuid         = generateUuid(getSuperGroup());
	detachAll(actions);
	getPool()->interruptableThreads.create_thread(
		boost::bind(&Group::finalizeRestart, this, shared_from_this(),
			options.copyAndPersist().clearPerRequestFields(),
			method, getPool()->spawnerFactory, restartsInitiated, actions),
		"Group restarter: " + name,
		POOL_HELPER_THREAD_STACK_SIZE
	);
}

// The 'self' parameter is for keeping the current Group object alive while this thread is running.
void
Group::finalizeRestart(GroupPtr self, Options options, RestartMethod method,
	SpawnerFactoryPtr spawnerFactory, unsigned int restartsInitiated,
	vector<Callback> postLockActions)
{
	TRACE_POINT();

	Pool::runAllActions(postLockActions);
	postLockActions.clear();

	this_thread::disable_interruption di;
	this_thread::disable_syscall_interruption dsi;

	// Create a new spawner.
	SpawnerPtr newSpawner = spawnerFactory->create(options);
	SpawnerPtr oldSpawner;

	UPDATE_TRACE_POINT();
	PoolPtr pool = getPool();

	Pool::DebugSupportPtr debug = pool->debugSupport;
	if (debug != NULL && debug->restarting) {
		this_thread::restore_interruption ri(di);
		this_thread::restore_syscall_interruption rsi(dsi);
		this_thread::interruption_point();
		debug->debugger->send("About to end restarting");
		debug->messages->recv("Finish restarting");
	}

	ScopedLock l(pool->syncher);
	if (!isAlive()) {
		P_DEBUG("Group " << name << " is shutting down, so aborting restart");
		return;
	}
	if (restartsInitiated != this->restartsInitiated) {
		// Before this restart could be finalized, another restart command was given.
		// The spawner we just created might be out of date now so we abort.
		P_DEBUG("Restart of group " << name << " aborted because a new restart was initiated concurrently");
		if (debug != NULL && debug->restarting) {
			debug->debugger->send("Restarting aborted");
		}
		return;
	}

	// Run some sanity checks.
	pool->fullVerifyInvariants();
	assert(m_restarting);
	UPDATE_TRACE_POINT();

	// Atomically swap the new spawner with the old one.
	resetOptions(options);
	oldSpawner = spawner;
	spawner    = newSpawner;

	m_restarting = false;
	if (shouldSpawn()) {
		spawn();
	} else if (isWaitingForCapacity()) {
		P_INFO("Group " << name << " is waiting for capacity to become available. "
			"Trying to shutdown another idle process to free capacity...");
		if (pool->forceFreeCapacity(this, postLockActions) != NULL) {
			spawn();
		} else {
			P_INFO("There are no processes right now that are eligible "
				"for shutdown. Will try again later.");
		}
	}
	verifyInvariants();

	l.unlock();
	oldSpawner.reset();
	Pool::runAllActions(postLockActions);
	P_DEBUG("Restart of group " << name << " done");
	if (debug != NULL && debug->restarting) {
		debug->debugger->send("Restarting done");
	}
}

/**
 * The `immediately` parameter only has effect if the detached processes checker
 * thread is active. It means that, if the thread is currently sleeping, it should
 * wake up immediately and perform work.
 */
void
Group::startCheckingDetachedProcesses(bool immediately) {
	if (!detachedProcessesCheckerActive) {
		P_DEBUG("Starting detached processes checker");
		getPool()->nonInterruptableThreads.create_thread(
			boost::bind(&Group::detachedProcessesCheckerMain, this, shared_from_this()),
			"Detached processes checker: " + name,
			POOL_HELPER_THREAD_STACK_SIZE
		);
		detachedProcessesCheckerActive = true;
	} else if (detachedProcessesCheckerActive && immediately) {
		detachedProcessesCheckerCond.notify_all();
	}
}

void
Group::detachedProcessesCheckerMain(GroupPtr self) {
	TRACE_POINT();
	PoolPtr pool = getPool();

	Pool::DebugSupportPtr debug = pool->debugSupport;
	if (debug != NULL && debug->detachedProcessesChecker) {
		debug->debugger->send("About to start detached processes checker");
		debug->messages->recv("Proceed with starting detached processes checker");
	}

	boost::unique_lock<boost::mutex> lock(pool->syncher);
	while (true) {
		assert(detachedProcessesCheckerActive);

		if (getLifeStatus() == SHUT_DOWN || this_thread::interruption_requested()) {
			UPDATE_TRACE_POINT();
			P_DEBUG("Stopping detached processes checker");
			detachedProcessesCheckerActive = false;
			break;
		}

		UPDATE_TRACE_POINT();
		if (!detachedProcesses.empty()) {
			P_TRACE(2, "Checking whether any of the " << detachedProcesses.size() <<
				" detached processes have exited...");
			ProcessList::iterator it = detachedProcesses.begin();
			ProcessList::iterator end = detachedProcesses.end();
			while (it != end) {
				const ProcessPtr process = *it;
				switch (process->getLifeStatus()) {
				case Process::ALIVE:
					if (process->canTriggerShutdown()) {
						P_DEBUG("Detached process " << process->inspect() <<
							" has 0 active sessions now. Triggering shutdown.");
						process->triggerShutdown();
						assert(process->getLifeStatus() == Process::SHUTDOWN_TRIGGERED);
					}
					it++;
					break;
				case Process::SHUTDOWN_TRIGGERED:
					if (process->canCleanup()) {
						P_DEBUG("Detached process " << process->inspect() << " has shut down. Cleaning up associated resources.");
						process->cleanup();
						assert(process->getLifeStatus() == Process::DEAD);
						it++;
						removeProcessFromList(process, detachedProcesses);
					} else if (process->shutdownTimeoutExpired()) {
						P_WARN("Detached process " << process->inspect() <<
							" didn't shut down within " PROCESS_SHUTDOWN_TIMEOUT_DISPLAY
							". Forcefully killing it with SIGKILL.");
						kill(process->pid, SIGKILL);
						it++;
					} else {
						it++;
					}
					break;
				default:
					P_BUG("Unknown 'lifeStatus' state " << (int) process->getLifeStatus());
				}
			}
		}

		UPDATE_TRACE_POINT();
		if (detachedProcesses.empty()) {
			UPDATE_TRACE_POINT();
			P_DEBUG("Stopping detached processes checker");
			detachedProcessesCheckerActive = false;

			vector<Callback> actions;
			if (shutdownCanFinish()) {
				UPDATE_TRACE_POINT();
				finishShutdown(actions);
			}

			verifyInvariants();
			verifyExpensiveInvariants();
			lock.unlock();
			UPDATE_TRACE_POINT();
			runAllActions(actions);
			break;
		} else {
			UPDATE_TRACE_POINT();
			verifyInvariants();
			verifyExpensiveInvariants();
		}

		// Not all processes can be shut down yet. Sleep for a while unless
		// someone wakes us up.
		UPDATE_TRACE_POINT();
		detachedProcessesCheckerCond.timed_wait(lock,
			posix_time::milliseconds(100));
	}
}

void
Group::wakeUpGarbageCollector() {
	getPool()->garbageCollectionCond.notify_all();
}

bool
Group::poolAtFullCapacity() const {
	return getPool()->atFullCapacity(false);
}

bool
Group::anotherGroupIsWaitingForCapacity() const {
	return findOtherGroupWaitingForCapacity() != NULL;
}

boost::shared_ptr<Group>
Group::findOtherGroupWaitingForCapacity() const {
	PoolPtr pool = getPool();
	StringMap<SuperGroupPtr>::const_iterator sg_it, sg_end = pool->superGroups.end();
	for (sg_it = pool->superGroups.begin(); sg_it != sg_end; sg_it++) {
		pair<StaticString, SuperGroupPtr> p = *sg_it;
		vector<GroupPtr>::const_iterator g_it, g_end = p.second->groups.end();
		for (g_it = p.second->groups.begin(); g_it != g_end; g_it++) {
			if (g_it->get() != this && (*g_it)->isWaitingForCapacity()) {
				return *g_it;
			}
		}
	}
	return GroupPtr();
}

ProcessPtr
Group::poolForceFreeCapacity(const Group *exclude, vector<Callback> &postLockActions) {
	return getPool()->forceFreeCapacity(exclude, postLockActions);
}

bool
Group::testOverflowRequestQueue() const {
	// This has a performance penalty, although I'm not sure whether the penalty is
	// any greater than a hash table lookup if I were to implement it in Options.
	Pool::DebugSupportPtr debug = getPool()->debugSupport;
	if (debug) {
		return debug->testOverflowRequestQueue;
	} else {
		return false;
	}
}

const ResourceLocator &
Group::getResourceLocator() const {
	return getPool()->getSpawnerConfig()->resourceLocator;
}

// 'process' is not a reference so that bind(runAttachHooks, ...) causes the shared
// pointer reference to increment.
void
Group::runAttachHooks(const ProcessPtr process) const {
	getPool()->runHookScripts("attached_process",
		boost::bind(&Group::setupAttachOrDetachHook, this, process, _1));
}

void
Group::runDetachHooks(const ProcessPtr process) const {
	getPool()->runHookScripts("detached_process",
		boost::bind(&Group::setupAttachOrDetachHook, this, process, _1));
}

void
Group::setupAttachOrDetachHook(const ProcessPtr process, HookScriptOptions &options) const {
	options.environment.push_back(make_pair("PASSENGER_PROCESS_PID", toString(process->pid)));
	options.environment.push_back(make_pair("PASSENGER_APP_ROOT", this->options.appRoot));
}

string
Group::generateSecret(const SuperGroupPtr &superGroup) {
	return superGroup->getPool()->getRandomGenerator()->generateAsciiString(43);
}

string
Group::generateUuid(const SuperGroupPtr &superGroup) {
	return superGroup->getPool()->getRandomGenerator()->generateAsciiString(20);
}


PoolPtr
Process::getPool() const {
	assert(getLifeStatus() != DEAD);
	return getGroup()->getPool();
}

SuperGroupPtr
Process::getSuperGroup() const {
	assert(getLifeStatus() != DEAD);
	return getGroup()->getSuperGroup();
}

void
Process::sendAbortLongRunningConnectionsMessage(const string &address) {
	boost::function<void ()> func = boost::bind(
		realSendAbortLongRunningConnectionsMessage, address);
	return getPool()->nonInterruptableThreads.create_thread(
		boost::bind(runAndPrintExceptions, func, false),
		"Sending detached message to process " + toString(pid),
		256 * 1024);
}

void
Process::realSendAbortLongRunningConnectionsMessage(string address) {
	TRACE_POINT();
	FileDescriptor fd(connectToServer(address));
	unsigned long long timeout = 3000000;
	vector<string> args;

	UPDATE_TRACE_POINT();
	args.push_back("abort_long_running_connections");
	writeArrayMessage(fd, args, &timeout);
}

string
Process::inspect() const {
	assert(getLifeStatus() != DEAD);
	stringstream result;
	result << "(pid=" << pid;
	GroupPtr group = getGroup();
	if (group != NULL) {
		// This Process hasn't been attached to a Group yet.
		result << ", group=" << group->name;
	}
	result << ")";
	return result.str();
}


const string &
Session::getConnectPassword() const {
	return getProcess()->connectPassword;
}

pid_t
Session::getPid() const {
	return getProcess()->pid;
}

const string &
Session::getGupid() const {
	return getProcess()->gupid;
}

unsigned int
Session::getStickySessionId() const {
	return getProcess()->stickySessionId;
}

const GroupPtr
Session::getGroup() const {
	return getProcess()->getGroup();
}

void
Session::requestOOBW() {
	ProcessPtr process = getProcess();
	assert(process->isAlive());
	process->getGroup()->requestOOBW(process);
}

int
Session::kill(int signo) {
	return getProcess()->kill(signo);
}


PipeWatcher::DataCallback PipeWatcher::onData;

PipeWatcher::PipeWatcher(const FileDescriptor &_fd, const char *_name, pid_t _pid)
	: fd(_fd),
	  name(_name),
	  pid(_pid)
{
	started = false;
}

void
PipeWatcher::initialize() {
	oxt::thread(boost::bind(threadMain, shared_from_this()),
		"PipeWatcher: PID " + toString(pid) + " " + name + ", fd " + toString(fd),
		POOL_HELPER_THREAD_STACK_SIZE);
}

void
PipeWatcher::start() {
	boost::lock_guard<boost::mutex> lock(startSyncher);
	started = true;
	startCond.notify_all();
}

void
PipeWatcher::threadMain(boost::shared_ptr<PipeWatcher> self) {
	TRACE_POINT();
	self->threadMain();
}

void
PipeWatcher::threadMain() {
	TRACE_POINT();
	{
		boost::unique_lock<boost::mutex> lock(startSyncher);
		while (!started) {
			startCond.wait(lock);
		}
	}

	UPDATE_TRACE_POINT();
	while (!this_thread::interruption_requested()) {
		char buf[1024 * 8];
		ssize_t ret;

		UPDATE_TRACE_POINT();
		ret = syscalls::read(fd, buf, sizeof(buf));
		if (ret == 0) {
			break;
		} else if (ret == -1) {
			UPDATE_TRACE_POINT();
			if (errno == ECONNRESET) {
				break;
			} else if (errno != EAGAIN) {
				int e = errno;
				P_WARN("Cannot read from process " << pid << " " << name <<
					": " << strerror(e) << " (errno=" << e << ")");
				break;
			}
		} else if (ret == 1 && buf[0] == '\n') {
			UPDATE_TRACE_POINT();
			printAppOutput(pid, name, "", 0);
		} else {
			UPDATE_TRACE_POINT();
			vector<StaticString> lines;
			ssize_t ret2 = ret;
			if (ret2 > 0 && buf[ret2 - 1] == '\n') {
				ret2--;
			}
			split(StaticString(buf, ret2), '\n', lines);
			foreach (const StaticString line, lines) {
				printAppOutput(pid, name, line.data(), line.size());
			}
		}

		if (onData != NULL) {
			onData(buf, ret);
		}
	}
}


} // namespace ApplicationPool2
} // namespace Passenger

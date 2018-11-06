/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2018 Phusion Holding B.V.
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
#include <IOTools/MessageSerialization.h>

/*************************************************************************
 *
 * Out-of-band work functions for ApplicationPool2::Group
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


/** Returns whether it is allowed to perform a new OOBW in this group. */
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

/** Returns whether a new OOBW should be initiated for this process. */
bool
Group::shouldInitiateOobw(Process *process) const {
	return process->oobwStatus == Process::OOBW_REQUESTED
		&& process->enabled != Process::DETACHED
		&& process->isAlive()
		&& oobwAllowed();
}

void
Group::maybeInitiateOobw(Process *process) {
	if (shouldInitiateOobw(process)) {
		// We keep an extra reference to prevent premature destruction.
		ProcessPtr p = process->shared_from_this();
		initiateOobw(p);
	}
}

// The 'self' parameter is for keeping the current Group object alive
void
Group::lockAndMaybeInitiateOobw(const ProcessPtr &process, DisableResult result, GroupPtr self) {
	TRACE_POINT();

	// Standard resource management boilerplate stuff...
	Pool *pool = getPool();
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
			if (shouldInitiateOobw(process.get())) {
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
	boost::this_thread::disable_interruption di;
	boost::this_thread::disable_syscall_interruption dsi;

	Socket *socket;
	Connection connection;
	Pool *pool = getPool();
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
		socket = process->findSocketsAcceptingHttpRequestsAndWithLowestBusyness();
	}

	UPDATE_TRACE_POINT();
	unsigned long long timeout = 1000 * 1000 * 60; // 1 min
	try {
		boost::this_thread::restore_interruption ri(di);
		boost::this_thread::restore_syscall_interruption rsi(dsi);

		// Grab a connection. The connection is marked as fail in order to
		// ensure it is closed / recycled after this request (otherwise we'd
		// need to completely read the response).
		connection = socket->checkoutConnection();
		connection.fail = true;
		ScopeGuard guard(boost::bind(&Socket::checkinConnection, socket, connection));

		// This is copied from Core::Controller when it is sending data using the
		// "session" protocol.
		char sizeField[sizeof(boost::uint32_t)];
		boost::container::small_vector<StaticString, 10> data;

		data.push_back(StaticString(sizeField, sizeof(boost::uint32_t)));
		data.push_back(P_STATIC_STRING_WITH_NULL("REQUEST_METHOD"));
		data.push_back(P_STATIC_STRING_WITH_NULL("OOBW"));

		data.push_back(P_STATIC_STRING_WITH_NULL("PASSENGER_CONNECT_PASSWORD"));
		data.push_back(getApiKey().toStaticString());
		data.push_back(StaticString("", 1));

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
	boost::container::vector<Callback> actions;
	{
		// Standard resource management boilerplate stuff...
		Pool *pool = getPool();
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
		if (shouldInitiateOobw(process.get())) {
			// We keep an extra reference to processes to prevent premature destruction.
			ProcessPtr p = process;
			initiateOobw(p);
			return;
		}
	}
}


/****************************
 *
 * Public methods
 *
 ****************************/


// Thread-safe, but only call outside the pool lock!
void
Group::requestOOBW(const ProcessPtr &process) {
	// Standard resource management boilerplate stuff...
	Pool *pool = getPool();
	boost::unique_lock<boost::mutex> lock(pool->syncher);
	if (isAlive() && process->isAlive() && process->oobwStatus == Process::OOBW_NOT_ACTIVE) {
		process->oobwStatus = Process::OOBW_REQUESTED;
	}
}


} // namespace ApplicationPool2
} // namespace Passenger

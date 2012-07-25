/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2011, 2012 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_
#define _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/thread.hpp>
#include <vector>
#include <utility>
#include <Logging.h>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/ComponentInfo.h>
#include <ApplicationPool2/Group.h>
#include <ApplicationPool2/Options.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * Except for otherwise documented parts, this class is not thread-safe,
 * so only access within ApplicationPool lock.
 */
class SuperGroup: public enable_shared_from_this<SuperGroup> {
public:
	enum State {
		/** This SuperGroup is being initialized. 'groups' is empty and
		 * get() actions cannot be immediately satisfied, so they
		 * are placed in getWaitlist. Once the SuperGroup is done
		 * loading the state it will transition to READY. Calling destroy()
		 * will make it transition to DESTROYING. If initialization
		 * failed it will transition to DESTROYED.
		 */
		INITIALIZING,
		
		/** This SuperGroup is loaded and is ready for action. From
		 * here the state can transition to RESTARTING or DESTROYING.
		 */
		READY,
		
		/** This SuperGroup is being restarted. The SuperGroup
		 * information is being reloaded from the data source
		 * and processes are being restarted. In this state
		 * get() actions can still be statisfied, and the data
		 * structures still contain the old information. Once reloading
		 * is done the data structures will be atomically swapped
		 * with the newly reloaded ones.
		 * Once the restart is completed, the state will transition
		 * to READY.
		 * Re-restarting won't have any effect in this state.
		 * destroy() will cause the restart to be aborted and will
		 * cause a transition to DESTROYING.
		 */
		RESTARTING,
		
		/** This SuperGroup is being destroyed. Processes are being shut
		 * down and other resources are being cleaned up. In this state,
		 * 'groups' is empty.
		 * Restarting won't have any effect, but get() will cause a
		 * transition to INITIALIZING.
		 */
		DESTROYING,
		
		/** This SuperGroup has been destroyed and all resources have been
		 * freed. Restarting won't have any effect but calling get() will
		 * make it transition to INITIALIZING.
		 */
		DESTROYED
	};
	
private:
	friend class Pool;
	friend class Group;
	
	Options options;
	/** A number for concurrency control, incremented every time the state changes.
	 * Every background thread in SuperGroup spawns knows the generation number
	 * from when the thread was spawned. A thread generally does some work outside
	 * the lock, then grabs the lock and updates the information in this SuperGroup
	 * with the results of the work. But before updating happens it first checks
	 * whether the generation number is as expected, so increasing this generation
	 * number will prevent old threads from updating the information with possibly
	 * now-stale information. It is a good way to prevent A-B-A concurrency
	 * problems.
	 */
	unsigned int generation;
	
	
	// Thread-safe.
	static boost::mutex &getPoolSyncher(const PoolPtr &pool);
	static void runAllActions(const vector<Callback> &actions);
	string generateSecret() const;
	
	void createInterruptableThread(const function<void ()> &func, const string &name,
		unsigned int stackSize);
	void createNonInterruptableThread(const function<void ()> &func, const string &name,
		unsigned int stackSize);
	
	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		
		P_ASSERT(groups.empty() ==
			(state == INITIALIZING || state == DESTROYING || state == DESTROYED));
		P_ASSERT((defaultGroup == NULL) ==
			(state == INITIALIZING || state == DESTROYING || state == DESTROYED));
		P_ASSERT(!( state == READY || state == RESTARTING || state == DESTROYING || state == DESTROYED ) ||
			( getWaitlist.empty() ));
	}
	
	void setState(State newState) {
		state = newState;
		generation++;
	}
	
	vector<ComponentInfo> loadComponentInfos(const Options &options) const {
		vector<ComponentInfo> infos;
		ComponentInfo info;
		info.name = "default";
		info.isDefault = true;
		infos.push_back(info);
		return infos;
	}
	
	Group *findDefaultGroup(const vector<GroupPtr> &groups) const {
		vector<GroupPtr>::const_iterator it;
		
		for (it = groups.begin(); it != groups.end(); it++) {
			const GroupPtr &group = *it;
			if (group->componentInfo.isDefault) {
				return group.get();
			}
		}
		return NULL;
	}
	
	pair<GroupPtr, unsigned int> findGroupCorrespondingToComponent(
		const vector<GroupPtr> &groups, const ComponentInfo &info) const
	{
		unsigned int i;
		for (i = 0; i < groups.size(); i++) {
			const GroupPtr &group = groups[i];
			if (group->componentInfo.name == info.name) {
				return make_pair(const_cast<GroupPtr &>(group), i);
			}
		}
		return make_pair(GroupPtr(), 0);
	}
	
	void detachGroup(const GroupPtr &group, vector<Callback> &postLockActions) {
		group->detachAll(postLockActions);
		group->setSuperGroup(SuperGroupPtr());
		while (!group->getWaitlist.empty()) {
			getWaitlist.push(group->getWaitlist.front());
			group->getWaitlist.pop();
		}
		for (unsigned int i = 0; i < groups.size(); i++) {
			if (groups[i] == group) {
				groups.erase(groups.begin() + i);
				break;
			}
		}
	}
	
	void detachGroups(const vector<GroupPtr> &groups, vector<Callback> &postLockActions) {
		vector<GroupPtr>::const_iterator it, end = groups.end();
		
		for (it = groups.begin(); it != end; it++) {
			const GroupPtr &group = *it;
			// doRestart() may temporarily nullify elements in 'groups'.
			if (group != NULL) {
				detachGroup(group, postLockActions);
			}
		}
	}
	
	void assignGetWaitlistToGroups(vector<Callback> &postLockActions) {
		while (!getWaitlist.empty()) {
			GetWaiter &waiter = getWaitlist.front();
			Group *group = route(waiter.options);
			Options adjustedOptions = waiter.options;
			adjustOptions(adjustedOptions, group);
			SessionPtr session = group->get(adjustedOptions, waiter.callback);
			if (session != NULL) {
				postLockActions.push_back(boost::bind(
					waiter.callback, session, ExceptionPtr()));
			}
			getWaitlist.pop();
		}
	}
	
	void adjustOptions(Options &options, const Group *group) const {
		// No-op.
	}

	void doInitialize(SuperGroupPtr self, Options options, unsigned int generation) {
		try {
			realDoInitialize(options, generation);
		} catch (const thread_interrupted &) {
			// Return;
		}
	}

	void realDoInitialize(const Options &options, unsigned int generation) {
		vector<ComponentInfo> componentInfos;
		vector<ComponentInfo>::const_iterator it;
		ExceptionPtr exception;
		
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
			exception = make_shared<SpawnException>(
				message, message, false);
		}
		
		PoolPtr pool = getPool();
		if (OXT_UNLIKELY(pool == NULL)) {
			return;
		}
		
		vector<Callback> actions;
		{
			unique_lock<boost::mutex> lock(getPoolSyncher(pool));
			this_thread::disable_interruption di;
			this_thread::disable_syscall_interruption dsi;
			NOT_EXPECTING_EXCEPTIONS();
			if (OXT_UNLIKELY(getPool() == NULL || generation != this->generation)) {
				return;
			}
			P_TRACE(2, "Initialization of SuperGroup " << inspect() << " almost done; grabbed lock");
			P_ASSERT(state == INITIALIZING);
			verifyInvariants();
			
			if (componentInfos.empty()) {
				/* Somehow initialization failed. Maybe something has deleted
				 * the supergroup files while we're working.
				 */
				P_ASSERT(exception != NULL);
				setState(DESTROYED);
				
				actions.reserve(getWaitlist.size());
				while (!getWaitlist.empty()) {
					const GetWaiter &waiter = getWaitlist.front();
					actions.push_back(boost::bind(waiter.callback,
						SessionPtr(), exception));
					getWaitlist.pop();
				}
			} else {
				for (it = componentInfos.begin(); it != componentInfos.end(); it++) {
					const ComponentInfo &info = *it;
					GroupPtr group = make_shared<Group>(shared_from_this(),
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
		runAllActions(actions);
	}
	
	void doRestart(SuperGroupPtr self, Options options, unsigned int generation) {
		try {
			realDoRestart(options, generation);
		} catch (const thread_interrupted &) {
			// Return.
		}
	}

	void realDoRestart(const Options &options, unsigned int generation) {
		TRACE_POINT();
		vector<ComponentInfo> componentInfos = loadComponentInfos(options);
		vector<ComponentInfo>::const_iterator it;
		
		PoolPtr pool = getPool();
		if (OXT_UNLIKELY(pool == NULL)) {
			return;
		}
		
		unique_lock<boost::mutex> lock(getPoolSyncher(pool));
		if (OXT_UNLIKELY(getPool() == NULL || this->generation != generation)) {
			return;
		}
		P_ASSERT(state == RESTARTING);
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
			GroupPtr &group = result.first;
			if (group != NULL) {
				unsigned int index = result.second;
				group->componentInfo = info;
				updatedGroups.push_back(group);
				groups[index].reset();
			} else {
				// This is not an existing group but a new one,
				// so create it.
				group = make_shared<Group>(shared_from_this(),
					options, info);
				newGroups.push_back(group);
			}
			// allGroups must be in the same order as componentInfos.
			allGroups.push_back(group);
		}
		
		// Some components might have been deleted, so delete the
		// corresponding groups.
		detachGroups(groups, actions);
		
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
	
	void doDestroy(SuperGroupPtr self, unsigned int generation) {
		TRACE_POINT();
		PoolPtr pool = getPool();
		if (OXT_UNLIKELY(pool == NULL)) {
			return;
		}
		
		// In the future we can run more destruction code here,
		// without holding the lock. Note that any destruction
		// code may not interfere with doInitialize().
		
		lock_guard<boost::mutex> lock(getPoolSyncher(pool));
		if (OXT_UNLIKELY(getPool() == NULL || this->generation != generation)) {
			return;
		}
		
		UPDATE_TRACE_POINT();
		P_ASSERT(state == DESTROYING);
		verifyInvariants();
		state = DESTROYED;
		verifyInvariants();
	}
	
	/*********************/
	
	/*********************/
	
public:
	mutable boost::mutex backrefSyncher;
	weak_ptr<Pool> pool;
	
	State state;
	string name;
	string secret;
	
	/** Invariant:
	 * groups.empty() == (state == INITIALIZING || state == DESTROYING || state == DESTROYED)
	 */
	vector<GroupPtr> groups;
	
	/** Invariant:
	 * (defaultGroup == NULL) == (state == INITIALIZING || state == DESTROYING || state == DESTROYED)
	 */
	Group *defaultGroup;
	
	/**
	 * get() requests for this super group that cannot be immediately satisfied
	 * are put on this wait list, which must be processed as soon as the
	 * necessary resources have become free.
	 *
	 * Invariant:
	 *    if state == READY || state == RESTARTING || state == DESTROYING || state == DESTROYED:
	 *       getWaitlist.empty()
	 * Equivalently:
	 *    if state != INITIALIZING:
	 *       getWaitlist.empty()
	 * Equivalently:
	 *    if !getWaitlist.empty():
	 *       state == INITIALIZING
	 */
	queue<GetWaiter> getWaitlist;
	
	/** One MUST call initialize() after construction because shared_from_this()
	 * is not available in the constructor.
	 */
	SuperGroup(const PoolPtr &pool, const Options &options) {
		this->pool = pool;
		this->options = options.copyAndPersist();
		this->name = options.getAppGroupName();
		secret = generateSecret();
		state = INITIALIZING;
		defaultGroup = NULL;
		generation = 0;
	}

	void initialize() {
		createNonInterruptableThread(
			boost::bind(
				&SuperGroup::doInitialize,
				this,
				// Keep reference to self to prevent destruction.
				shared_from_this(),
				options.copyAndPersist(),
				generation),
			"SuperGroup initializer: " + name,
			POOL_HELPER_THREAD_STACK_SIZE);
	}
	
	// Thread-safe.
	PoolPtr getPool() const {
		lock_guard<boost::mutex> lock(backrefSyncher);
		return pool.lock();
	}
	
	// Thread-safe.
	void setPool(const PoolPtr &pool) {
		lock_guard<boost::mutex> lock(backrefSyncher);
		this->pool = pool;
	}
	
	// Thread-safe.
	bool detached() const {
		return getPool() == NULL;
	}
	
	const char *getStateName() const {
		switch (state) {
		case INITIALIZING:
			return "INITIALIZING";
		case READY:
			return "READY";
		case RESTARTING:
			return "RESTARTING";
		case DESTROYING:
			return "DESTROYING";
		case DESTROYED:
			return "DESTROYED";
		default:
			abort();
		}
	}

	/**
	 * If allowReinitialization is true then destroying a SuperGroup that
	 * has get waiters will make it reinitialize. Otherwise this SuperGroup
	 * will be forcefully set to the DESTROYING state and getWaitlist will be
	 * left untouched; in this case it is up to the caller to the empty
	 * the getWaitlist and do something with it, otherwise the invariant
	 * will be broken.
	 */
	void destroy(vector<Callback> &postLockActions, bool allowReinitialization = true) {
		verifyInvariants();
		switch (state) {
		case INITIALIZING:
		case READY:
		case RESTARTING:
			detachGroups(groups, postLockActions);
			defaultGroup = NULL;
			if (getWaitlist.empty() || !allowReinitialization) {
				setState(DESTROYING);
				createNonInterruptableThread(
					boost::bind(
						&SuperGroup::doDestroy,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						generation),
					"SuperGroup destroyer: " + name,
					POOL_HELPER_THREAD_STACK_SIZE + 1024 * 256);
			} else {
				// Spawning this thread before setState() so that
				// it doesn't change the state when done.
				createNonInterruptableThread(
					boost::bind(
						&SuperGroup::doDestroy,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						generation),
					"SuperGroup destroyer: " + name,
					POOL_HELPER_THREAD_STACK_SIZE + 1024 * 256);
				setState(INITIALIZING);
				createNonInterruptableThread(
					boost::bind(
						&SuperGroup::doInitialize,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						options.copyAndPersist(),
						generation),
					"SuperGroup initializer: " + name,
					1024 * 64);
			}
			break;
		case DESTROYING:
		case DESTROYED:
			break;
		default:
			abort();
		}
		if (allowReinitialization) {
			verifyInvariants();
		}
	}
	
	/**
	 * @post
	 *    if result:
	 *       getWaitlist.empty()
	 */
	bool garbageCollectable(unsigned long long now = 0) const {
		if (state == READY) {
			vector<GroupPtr>::const_iterator it, end = groups.end();
			bool result = true;
			
			for (it = groups.begin(); result && it != end; it++) {
				result = result && (*it)->garbageCollectable(now);
			}
			P_ASSERT(!result || getWaitlist.empty());
			return result;
		} else {
			P_ASSERT(!(state == DESTROYED) || getWaitlist.empty());
			return state == DESTROYED;
		}
	}
	
	SessionPtr get(const Options &newOptions, const GetCallback &callback) {
		switch (state) {
		case INITIALIZING:
			getWaitlist.push(GetWaiter(newOptions, callback));
			verifyInvariants();
			return SessionPtr();
		case READY:
		case RESTARTING:
			if (needsRestart()) {
				restart(newOptions);
			}
			if (groups.size() > 1) {
				Group *group = route(newOptions);
				Options adjustedOptions = newOptions;
				adjustOptions(adjustedOptions, group);
				verifyInvariants();
				return group->get(adjustedOptions, callback);
			} else {
				verifyInvariants();
				return defaultGroup->get(newOptions, callback);
			}
		case DESTROYING:
		case DESTROYED:
			getWaitlist.push(GetWaiter(newOptions, callback));
			setState(INITIALIZING);
			createNonInterruptableThread(
				boost::bind(
					&SuperGroup::doInitialize,
					this,
					// Keep reference to self to prevent destruction.
					shared_from_this(),
					newOptions.copyAndPersist(),
					generation),
				"SuperGroup initializer: " + name,
				POOL_HELPER_THREAD_STACK_SIZE);
			verifyInvariants();
			return SessionPtr();
		default:
			abort();
			return SessionPtr(); // Shut up compiler warning.
		};
	}
	
	Group *route(const Options &options) const {
		return defaultGroup;
	}
	
	unsigned int utilization() const {
		vector<GroupPtr>::const_iterator it, end = groups.end();
		unsigned int result = 0;
		
		for (it = groups.begin(); it != end; it++) {
			result += (*it)->utilization();
		}
		if (state == INITIALIZING || state == RESTARTING) {
			result++;
		}
		return result;
	}
	
	bool needsRestart() const {
		return false;
	}
	
	void restart(const Options &options) {
		verifyInvariants();
		if (state == READY) {
			createInterruptableThread(
				boost::bind(
					&SuperGroup::doRestart,
					this,
					// Keep reference to self to prevent destruction.
					shared_from_this(),
					options.copyAndPersist(),
					generation),
				"SuperGroup restarter: " + name,
				POOL_HELPER_THREAD_STACK_SIZE);
			state = RESTARTING;
		}
		verifyInvariants();
	}

	string inspect() const {
		return name;
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_ */

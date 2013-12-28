/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2013 Phusion
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
 * An abstract container for multiple Groups (applications). It is a support
 * structure for supporting application sets, multiple applications that can
 * closely work with each other as if they were a single entity. There's no
 * support for application sets yet in Phusion Passenger 4, but this class
 * lays the foundation to make it possible.
 *
 * An application set is backed by a directory that contains:
 *
 * - The files for the individual applications.
 * - An application set manifest file that:
 *   * Describes the containing applications.
 *   * Describes the application set itself.
 *   * Describes instructions that must be first
 *     followed before the application set is usable.
 *   * Describes instructions that must be followed when the
 *     application set is to be cleaned up.
 *
 * SuperGroup is designed to assume to that loading the manifest file
 * and following the instructions in them may be a blocking operation
 * that can take a while. Thus it makes use of background threads to
 * do most of initialization and destruction work (see `doInitialize()`
 * and `doDestroy()`). The `state` variable keeps track of things.
 *
 * A SuperGroup starts off in the `INITIALIZING` state. When it's done
 * initializing, it becomes `READY`. If a restart is necessary it will
 * transition to `RESTARTING` and then eventually back to `READY`.
 * At any time the SuperGroup may be instructed to destroy itself, in
 * which case it will first transition to `DESTROYING` and eventually
 * to `DESTROYED`. Once destroyed, the SuperGroup is reusable so it
 * can go back to `INITIALIZING` when needed.
 *
 *
 * ## Life time
 *
 * A SuperGroup, once created and added to the Pool, is normally not
 * supposed to be destroyed and removed from the Pool automatically.
 * This is because a SuperGroup may contain important spawning
 * parameters such as SuperGroup-specific environment variables.
 * However the system does not disallow the administrator from
 * manually removing a SuperGroup from the pool.
 *
 *
 * ## Multiple instances and initialization/destruction
 *
 * It is allowed to create multiple SuperGroups backed by the same
 * application set directory, e.g. to increase concurrency. The system
 * may destroy a SuperGroup in the background while creating a new
 * one while that is in progress. This could even happen across processes,
 * e.g. one process is busy destroying a SuperGroup while another
 * one is initializing it.
 *
 * Furthermore, it is possible for a SuperGroup to receive a get()
 * command during destruction.
 *
 * It is therefore important that `doInitialize()` and `doDestroy()`
 * do not interfere with other instances of the same code, and can
 * commit their work atomically.
 *
 *
 * ## Thread-safety
 *
 * Except for otherwise documented parts, this class is not thread-safe,
 * so only access it within the ApplicationPool lock.
 */
class SuperGroup: public boost::enable_shared_from_this<SuperGroup> {
public:
	enum State {
		/** This SuperGroup is being initialized. `groups` is empty and
		 * `get()` actions cannot be immediately satisfied, so they
		 * are placed in `getWaitlist`. Once the SuperGroup is done
		 * loading the state it will transition to `READY`. Calling `destroy()`
		 * will make it transition to `DESTROYING`. If initialization
		 * failed it will transition to `DESTROYED`.
		 */
		INITIALIZING,
		
		/** This SuperGroup is loaded and is ready for action. From
		 * here the state can transition to `RESTARTING` or `DESTROYING`.
		 */
		READY,
		
		/** This SuperGroup is being restarted. The SuperGroup
		 * information is being reloaded from the data source
		 * and processes are being restarted. In this state
		 * `get()` actions can still be statisfied, and the data
		 * structures still contain the old information. Once reloading
		 * is done the data structures will be atomically swapped
		 * with the newly reloaded ones. The old structures will be
		 * destroyed in the background.
		 * Once the restart is completed, the state will transition
		 * to `READY`.
		 * Re-restarting won't have any effect in this state.
		 * `destroy()` will cause the restart to be aborted and will
		 * cause a transition to `DESTROYING`.
		 */
		RESTARTING,
		
		/** This SuperGroup is being destroyed. Processes are being shut
		 * down and other resources are being cleaned up. In this state,
		 * `groups` is empty.
		 * Restarting won't have any effect, but `get()` will cause a
		 * transition to `INITIALIZING`.
		 */
		DESTROYING,
		
		/** This SuperGroup has been destroyed and all resources have been
		 * freed. Restarting won't have any effect but calling `get()` will
		 * make it transition to `INITIALIZING`.
		 */
		DESTROYED
	};

	enum ShutdownResult {
		/** The SuperGroup has been successfully destroyed. */
		SUCCESS,
		/** The SuperGroup was not destroyed because a get or restart
		 * request came in while destroying.
		 */
		CANCELED
	};

	typedef boost::function<void (ShutdownResult result)> ShutdownCallback;
	
private:
	friend class Pool;
	friend class Group;
	
	Options options;
	/** A number for concurrency control, incremented every time the state changes.
	 * Every background thread that SuperGroup spawns knows the generation number
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
	void runInitializationHooks() const;
	void runDestructionHooks() const;
	void setupInitializationOrDestructionHook(HookScriptOptions &options) const;
	
	void createInterruptableThread(const boost::function<void ()> &func, const string &name,
		unsigned int stackSize);
	
	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		
		assert(groups.empty() ==
			(state == INITIALIZING || state == DESTROYING || state == DESTROYED));
		assert((defaultGroup == NULL) ==
			(state == INITIALIZING || state == DESTROYING || state == DESTROYED));
		assert(!( state == READY || state == RESTARTING || state == DESTROYING || state == DESTROYED ) ||
			( getWaitlist.empty() ));
		assert(!( state == DESTROYED ) || ( detachedGroups.empty() ));
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
	
	static void oneGroupHasBeenShutDown(SuperGroupPtr self, GroupPtr group) {
		// This function is either called from the pool event loop or directly from
		// the detachAllGroups post lock actions. In both cases getPool() is never NULL.
		PoolPtr pool = self->getPool();
		boost::lock_guard<boost::mutex> lock(self->getPoolSyncher(pool));

		vector<GroupPtr>::iterator it, end = self->detachedGroups.end();
		for (it = self->detachedGroups.begin(); it != end; it++) {
			if (*it == group) {
				self->detachedGroups.erase(it);
				break;
			}
		}
	}
	
	/** One of the post lock actions can potentially perform a long-running
	 * operation, so running them in a thread is advised.
	 */
	void detachAllGroups(vector<GroupPtr> &groups, vector<Callback> &postLockActions) {
		foreach (const GroupPtr &group, groups) {
			// doRestart() may temporarily nullify elements in 'groups'.
			if (group == NULL) {
				continue;
			}
			
			while (!group->getWaitlist.empty()) {
				getWaitlist.push_back(group->getWaitlist.front());
				group->getWaitlist.pop_front();
			}
			detachedGroups.push_back(group);
			group->shutdown(
				boost::bind(oneGroupHasBeenShutDown,
					shared_from_this(),
					group),
				postLockActions
			);
		}

		groups.clear();
	}
	
	void assignGetWaitlistToGroups(vector<Callback> &postLockActions) {
		while (!getWaitlist.empty()) {
			GetWaiter &waiter = getWaitlist.front();
			Group *group = route(waiter.options);
			Options adjustedOptions = waiter.options;
			adjustOptions(adjustedOptions, group);
			SessionPtr session = group->get(adjustedOptions, waiter.callback,
				postLockActions);
			if (session != NULL) {
				postLockActions.push_back(boost::bind(
					waiter.callback, session, ExceptionPtr()));
			}
			getWaitlist.pop_front();
		}
	}
	
	void adjustOptions(Options &options, const Group *group) const {
		// No-op.
	}

	static void doInitialize(SuperGroupPtr self, Options options, unsigned int generation) {
		self->realDoInitialize(options, generation);
	}

	static void doRestart(SuperGroupPtr self, Options options, unsigned int generation) {
		self->realDoRestart(options, generation);
	}

	void realDoInitialize(const Options &options, unsigned int generation);
	void realDoRestart(const Options &options, unsigned int generation);
	
	void doDestroy(SuperGroupPtr self, unsigned int generation, ShutdownCallback callback) {
		TRACE_POINT();
		
		runDestructionHooks();
		
		// Wait until 'detachedGroups' is empty.
		UPDATE_TRACE_POINT();
		PoolPtr pool = getPool();
		boost::unique_lock<boost::mutex> lock(getPoolSyncher(pool));
		verifyInvariants();
		while (true) {
			if (OXT_UNLIKELY(this->generation != generation)) {
				UPDATE_TRACE_POINT();
				lock.unlock();
				if (callback) {
					callback(CANCELED);
				}
				return;
			} else if (detachedGroups.empty()) {
				break;
			} else {
				UPDATE_TRACE_POINT();
				lock.unlock();
				syscalls::usleep(10000);
				lock.lock();
				verifyInvariants();
			}
		}
		
		UPDATE_TRACE_POINT();
		assert(state == DESTROYING);
		state = DESTROYED;
		verifyInvariants();

		lock.unlock();
		if (callback) {
			callback(SUCCESS);
		}
	}
	
	/*********************/
	
	/*********************/
	
public:
	mutable boost::mutex backrefSyncher;
	const boost::weak_ptr<Pool> pool;
	
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
	 * necessary resources have become free. Requests must wait when a SuperGroup
	 * is initializing.
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
	deque<GetWaiter> getWaitlist;

	/**
	 * Groups which are being shut down right now. These Groups contain a
	 * reference to the containg SuperGroup so that the SuperGroup is not
	 * actually destroyed until all Groups in this collection are done
	 * shutting down.
	 *
	 * Invariant:
	 *    if state == DESTROYED:
	 *       detachedGroups.empty()
	 */
	vector<GroupPtr> detachedGroups;
	
	/** One MUST call initialize() after construction because shared_from_this()
	 * is not available in the constructor.
	 */
	SuperGroup(const PoolPtr &_pool, const Options &options)
		: pool(_pool)
	{
		this->options = options.copyAndPersist().clearLogger();
		this->name = options.getAppGroupName();
		secret = generateSecret();
		state = INITIALIZING;
		defaultGroup = NULL;
		generation = 0;
	}

	~SuperGroup() {
		if (OXT_UNLIKELY(state != DESTROYED)) {
			P_BUG("You must call Group::destroy(..., false) before "
				"actually destroying the SuperGroup.");
		}
		verifyInvariants();
	}

	void initialize() {
		createInterruptableThread(
			boost::bind(
				doInitialize,
				shared_from_this(),
				options.copyAndPersist(),
				generation),
			"SuperGroup initializer: " + name,
			POOL_HELPER_THREAD_STACK_SIZE);
	}
	
	/**
	 * Thread-safe.
	 *
	 * As long as 'state' != DESTROYED, result != NULL.
	 * But in thread callbacks in this file, getPool() is never NULL
	 * because Pool::destroy() joins all threads, so Pool can never
	 * be destroyed before all thread callbacks have finished.
	 */
	PoolPtr getPool() const {
		return pool.lock();
	}

	bool isAlive() const {
		return state != DESTROYING && state != DESTROYED;
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
			P_BUG("Unknown SuperGroup state " << (int) state);
			return NULL; // Shut up compiler warning.
		}
	}

	/**
	 * If `allowReinitialization` is true then destroying a SuperGroup that
	 * has get waiters will make it reinitialize. Otherwise this SuperGroup
	 * will be forcefully set to the `DESTROYING` state and `getWaitlist` will be
	 * left untouched; in this case it is up to the caller to empty
	 * the `getWaitlist` and do something with it, otherwise the invariant
	 * will be broken.
	 *
	 * One of the post lock actions can potentially perform a long-running
	 * operation, so running them in a thread is advised.
	 */
	void destroy(bool allowReinitialization, vector<Callback> &postLockActions,
		const ShutdownCallback &callback)
	{
		verifyInvariants();
		switch (state) {
		case INITIALIZING:
		case READY:
		case RESTARTING:
			detachAllGroups(groups, postLockActions);
			defaultGroup = NULL;
			if (getWaitlist.empty() || !allowReinitialization) {
				setState(DESTROYING);
				createInterruptableThread(
					boost::bind(
						&SuperGroup::doDestroy,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						generation,
						callback),
					"SuperGroup destroyer: " + name,
					POOL_HELPER_THREAD_STACK_SIZE + 1024 * 256);
			} else {
				// Spawning this thread before setState() so that
				// it doesn't change the state when done.
				createInterruptableThread(
					boost::bind(
						&SuperGroup::doDestroy,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						generation,
						ShutdownCallback()),
					"SuperGroup destroyer: " + name,
					POOL_HELPER_THREAD_STACK_SIZE + 1024 * 256);
				setState(INITIALIZING);
				createInterruptableThread(
					boost::bind(
						doInitialize,
						shared_from_this(),
						options.copyAndPersist(),
						generation),
					"SuperGroup initializer: " + name,
					POOL_HELPER_THREAD_STACK_SIZE + 1024 * 256);
				if (callback) {
					postLockActions.push_back(boost::bind(callback, CANCELED));
				}
			}
			break;
		case DESTROYING:
		case DESTROYED:
			break;
		default:
			P_BUG("Unknown SuperGroup state " << (int) state);
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
		/* if (state == READY) {
			vector<GroupPtr>::const_iterator it, end = groups.end();
			bool result = true;
			
			for (it = groups.begin(); result && it != end; it++) {
				result = result && (*it)->garbageCollectable(now);
			}
			assert(!result || getWaitlist.empty());
			return result;
		} else {
			assert(!(state == DESTROYED) || getWaitlist.empty());
			return state == DESTROYED;
		} */
		return false;
	}
	
	SessionPtr get(const Options &newOptions, const GetCallback &callback,
		vector<Callback> &postLockActions)
	{
		switch (state) {
		case INITIALIZING:
			getWaitlist.push_back(GetWaiter(newOptions, callback));
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
				return group->get(adjustedOptions, callback, postLockActions);
			} else {
				verifyInvariants();
				return defaultGroup->get(newOptions, callback, postLockActions);
			}
		case DESTROYING:
		case DESTROYED:
			getWaitlist.push_back(GetWaiter(newOptions, callback));
			setState(INITIALIZING);
			createInterruptableThread(
				boost::bind(
					doInitialize,
					shared_from_this(),
					newOptions.copyAndPersist().clearLogger(),
					generation),
				"SuperGroup initializer: " + name,
				POOL_HELPER_THREAD_STACK_SIZE);
			verifyInvariants();
			return SessionPtr();
		default:
			P_BUG("Unknown SuperGroup state " << (int) state);
			return SessionPtr(); // Shut up compiler warning.
		};
	}
	
	Group *route(const Options &options) const {
		return defaultGroup;
	}
	
	unsigned int capacityUsed() const {
		vector<GroupPtr>::const_iterator it, end = groups.end();
		unsigned int result = 0;
		
		for (it = groups.begin(); it != end; it++) {
			result += (*it)->capacityUsed();
		}
		if (state == INITIALIZING || state == RESTARTING) {
			result++;
		}
		return result;
	}

	unsigned int getProcessCount() const {
		unsigned int result = 0;
		vector<GroupPtr>::const_iterator g_it, g_end = groups.end();
		for (g_it = groups.begin(); g_it != g_end; g_it++) {
			const GroupPtr &group = *g_it;
			result += group->getProcessCount();
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
					doRestart,
					// Keep reference to self to prevent destruction.
					shared_from_this(),
					options.copyAndPersist().clearLogger(),
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

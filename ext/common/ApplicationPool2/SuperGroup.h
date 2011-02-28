#ifndef _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_
#define _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/thread.hpp>
#include <vector>
#include <utility>
#include <cassert>
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
		 * are placed in getWaitlist. Once the SuperGroups is done
		 * loading the state will transition to READY. Calling destroy()
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
		 * get() actions can still be statisfied.
		 * Once the restart is completed, the state will transition
		 * to READY.
		 * Re-restarting won't have any effect in this state.
		 * destroy() will cause the restart to the aborted and will
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
	
	struct GetAction {
		Options options;
		GetCallback callback;
		
		GetAction(const Options &o, const GetCallback &cb)
			: options(o),
			  callback(cb)
		{
			options.persist(o);
		}
	};
	
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
	
	void createInterruptableThread(const function<void ()> &func, const string &name,
		unsigned int stackSize);
	void createNonInterruptableThread(const function<void ()> &func, const string &name,
		unsigned int stackSize);
	
	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		
		assert(groups.empty() ==
			(state == INITIALIZING || state == DESTROYING || state == DESTROYED));
		assert((defaultGroup == NULL) ==
			(state == INITIALIZING || state == DESTROYING || state == DESTROYED));
		assert(!( state == READY || state == RESTARTING || state == DESTROYING || state == DESTROYED ) ||
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
	
	void forceDetachGroup(const GroupPtr &group) {
		group->detachAll();
		group->setSuperGroup(SuperGroupPtr());
		while (!group->getWaitlist.empty()) {
			GetCallback callback = group->getWaitlist.front();
			group->getWaitlist.pop();
			getWaitlist.push(GetAction(group->options, callback));
		}
	}
	
	void forceDetachGroups(const vector<GroupPtr> &groups) {
		vector<GroupPtr>::const_iterator it, end = groups.end();
		
		for (it = groups.begin(); it != end; it++) {
			const GroupPtr &group = *it;
			// doRestart() may temporarily nullify elements in 'groups'.
			if (group != NULL) {
				forceDetachGroup(group);
			}
		}
	}
	
	void assignGetWaitlistToGroups() {
		while (!getWaitlist.empty()) {
			GetAction &action = getWaitlist.front();
			Group *group = route(action.options);
			Options adjustedOptions = action.options;
			adjustOptions(adjustedOptions, group);
			group->get(adjustedOptions, action.callback);
			getWaitlist.pop();
		}
	}
	
	void adjustOptions(Options &options, const Group *group) const {
		// No-op.
	}
	
	void initialize(SuperGroupPtr self, Options options, unsigned int generation) {
		vector<ComponentInfo> componentInfos;
		vector<ComponentInfo>::const_iterator it;
		ExceptionPtr exception;
		
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
		
		unique_lock<boost::mutex> lock(getPoolSyncher(pool));
		if (OXT_UNLIKELY(generation != this->generation)) {
			return;
		}
		assert(state == INITIALIZING);
		verifyInvariants();
		
		if (componentInfos.empty()) {
			// Somehow initialization failed. Maybe something has deleted
			// the supergroup files while we're working.
			setState(DESTROYED);
			
			vector<Callback> callbacks;
			callbacks.reserve(getWaitlist.size());
			while (!getWaitlist.empty()) {
				GetAction &action = getWaitlist.front();
				// TODO: generate proper exception
				callbacks.push_back(boost::bind(action.callback,
					SessionPtr(), ExceptionPtr()));
				getWaitlist.pop();
			}
			lock.unlock();
			
			vector<Callback>::const_iterator it;
			for (it = callbacks.begin(); it != callbacks.end(); it++) {
				const Callback &callback = *it;
				callback();
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
			assignGetWaitlistToGroups();
		}
		
		verifyInvariants();
	}
	
	void doRestart(SuperGroupPtr self, Options options, unsigned int generation) {
		vector<ComponentInfo> componentInfos = loadComponentInfos(options);
		vector<ComponentInfo>::const_iterator it;
		
		PoolPtr pool = getPool();
		if (OXT_UNLIKELY(pool == NULL)) {
			return;
		}
		
		lock_guard<boost::mutex> lock(getPoolSyncher(pool));
		if (OXT_UNLIKELY(this->generation != generation)) {
			return;
		}
		assert(state == RESTARTING);
		verifyInvariants();
		
		vector<GroupPtr> allGroups;
		vector<GroupPtr> updatedGroups;
		vector<GroupPtr> newGroups;
		vector<GroupPtr>::const_iterator g_it;
		this->options = options;
		
		// Update the component information for existing groups.
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
		forceDetachGroups(groups);
		
		// Tell all previous existing groups to restart.
		for (g_it = updatedGroups.begin(); g_it != updatedGroups.end(); g_it++) {
			GroupPtr group = *g_it;
			group->restart(options);
		}
		
		groups = allGroups;
		defaultGroup = findDefaultGroup(allGroups);
		setState(READY);
		assignGetWaitlistToGroups();
		
		verifyInvariants();
	}
	
	void doDestroy(SuperGroupPtr self, unsigned int generation) {
		PoolPtr pool = getPool();
		if (OXT_UNLIKELY(pool == NULL)) {
			return;
		}
		
		// In the future we can run more destruction code here,
		// without holding the lock. Note that any destruction
		// code may not interfere with initialize().
		
		lock_guard<boost::mutex> lock(getPoolSyncher(pool));
		if (OXT_UNLIKELY(this->generation != generation)) {
			return;
		}
		
		assert(state == DESTROYING);
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
	 * @invariant
	 *    if state == READY || state == RESTARTING || state == DESTROYING || state == DESTROYED:
	 *       getWaitlist.empty()
	 */
	queue<GetAction> getWaitlist;
	
	SuperGroup(const PoolPtr &pool, const Options &options) {
		this->pool = pool;
		state = INITIALIZING;
		defaultGroup = NULL;
		this->options = options.copyAndPersist();
		generation = 0;
		createNonInterruptableThread(
			boost::bind(
				&SuperGroup::initialize,
				this,
				// Keep reference to self to prevent destruction.
				shared_from_this(),
				options.copyAndPersist(),
				generation),
			"SuperGroup initializer",
			1024 * 64);
	}
	
	~SuperGroup() {
		destroy();
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
	
	void destroy() {
		switch (state) {
		case INITIALIZING:
		case READY:
		case RESTARTING:
			forceDetachGroups(groups);
			defaultGroup = NULL;
			if (getWaitlist.empty()) {
				setState(DESTROYING);
				createNonInterruptableThread(
					boost::bind(
						&SuperGroup::doDestroy,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						generation),
					"SuperGroup destroyer",
					1024 * 256);
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
					"SuperGroup destroyer",
					1024 * 256);
				setState(INITIALIZING);
				createNonInterruptableThread(
					boost::bind(
						&SuperGroup::initialize,
						this,
						// Keep reference to self to prevent destruction.
						shared_from_this(),
						options.copyAndPersist(),
						generation),
					"SuperGroup initializer",
					1024 * 64);
			}
			break;
		case DESTROYING:
		case DESTROYED:
			break;
		default:
			abort();
		}
	}
	
	void garbageCollect() {
		//if (all groups are garbage collectable) {
		//	cleanup();
		//}
	}
	
	SessionPtr get(const Options &newOptions, const GetCallback &callback) {
		switch (state) {
		case INITIALIZING:
			getWaitlist.push(GetAction(newOptions, callback));
			verifyInvariants();
			break;
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
			break;
		case DESTROYING:
		case DESTROYED:
			getWaitlist.push(GetAction(newOptions, callback));
			setState(INITIALIZING);
			createNonInterruptableThread(
				boost::bind(
					&SuperGroup::initialize,
					this,
					// Keep reference to self to prevent destruction.
					shared_from_this(),
					newOptions.copyAndPersist(),
					generation),
				"SuperGroup initializer",
				1024 * 64);
			verifyInvariants();
			break;
		default:
			abort();
		};
	}
	
	Group *route(const Options &options) const {
		return defaultGroup;
	}
	
	unsigned int usage() const {
		vector<GroupPtr>::const_iterator it, end = groups.end();
		unsigned int result = 0;
		
		for (it = groups.begin(); it != end; it++) {
			result += (*it)->usage();
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
				"SuperGroup restarter",
				1024 * 64);
			state = RESTARTING;
		}
		verifyInvariants();
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_ */

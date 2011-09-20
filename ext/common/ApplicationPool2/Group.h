#ifndef _PASSENGER_APPLICATION_POOL2_GROUP_H_
#define _PASSENGER_APPLICATION_POOL2_GROUP_H_

#include <string>
#include <map>
#include <queue>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <oxt/macros.hpp>
#include <oxt/thread.hpp>
#include <cassert>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/ComponentInfo.h>
#include <ApplicationPool2/Spawner.h>
#include <ApplicationPool2/Process.h>
#include <ApplicationPool2/Options.h>
#include <Utils/CachedFileStat.hpp>
#include <Utils/FileChangeChecker.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


/**
 * Except for otherwise documented parts, this class is not thread-safe,
 * so only access within ApplicationPool lock.
 */
class Group: public enable_shared_from_this<Group> {
private:
	friend class Pool;
	friend class SuperGroup;
	
	struct GetAction {
		GetCallback callback;
		SessionPtr session;
	};
	
	mutable boost::mutex backrefSyncher;
	weak_ptr<SuperGroup> superGroup;
	CachedFileStat cstat;
	FileChangeChecker fileChangeChecker;
	string restartFile;
	string alwaysRestartFile;
	
	
	static void _onSessionClose(Session *session) {
		const ProcessPtr &process = session->getProcess();
		GroupPtr group = process->getGroup();
		if (OXT_LIKELY(group != NULL)) {
			group->onSessionClose(process, session);
		}
	}
	
	void createInterruptableThread(const function<void ()> &func, const string &name,
		unsigned int stackSize);
	string generateSecret() const;
	void onSessionClose(const ProcessPtr &process, Session *session);
	void spawnThreadMain(GroupPtr self, SpawnerPtr spawner, Options options);
	
	void verifyInvariants() const {
		// !a || b: logical equivalent of a IMPLIES b.
		
		// Verify processes and pqueue invariants.
		assert(processes.empty() == (pqueue.top() == NULL));
		assert(count >= 0);
		assert(processes.empty() == (count == 0));
		
		// Verify getWaitlist invariants.
		assert(!( !getWaitlist.empty() ) || ( processes.empty() || pqueue.top()->atFullCapacity() ));
		assert(!( !processes.empty() && !pqueue.top()->atFullCapacity() ) || ( getWaitlist.empty() ));
		assert(!( processes.empty() && !spawning() ) || ( getWaitlist.empty() ));
		assert(!( !getWaitlist.empty() ) || ( !processes.empty() || spawning() ));
	}
	
	void resetOptions(const Options &newOptions) {
		options = newOptions;
		options.persist(newOptions);
		options.clearPerRequestFields();
		options.groupSecret = secret;
	}
	
	void mergeOptions(const Options &other) {
		options.maxRequests      = other.maxRequests;
		options.minProcesses     = other.minProcesses;
		options.statThrottleRate = other.statThrottleRate;
		options.spawnerTimeout   = other.spawnerTimeout;
	}
	
	void runAllActions(const vector<Callback> &actions) {
		vector<Callback>::const_iterator it, end = actions.end();
		for (it = actions.begin(); it != end; it++) {
			(*it)();
		}
	}
	
	/** @pre count > 0 */
	SessionPtr newSession() {
		Process *process   = pqueue.top();
		SessionPtr session = process->newSession();
		session->onClose   = _onSessionClose;
		pqueue.pop();
		process->pqHandle  = pqueue.push(process, process->usage());
		return session;
	}
	
	template<typename Lock>
	void assignSessionsToGetWaitersQuickly(Lock &lock) {
		if (OXT_LIKELY(getWaitlist.size() < 50)) {
			GetAction actions[getWaitlist.size()];
			unsigned int count = 0;
			while (!getWaitlist.empty() && pqueue.top() != NULL && !pqueue.top()->atFullCapacity()) {
				actions[count].callback = getWaitlist.front().callback;
				actions[count].session  = newSession();
				getWaitlist.pop();
				count++;
			}
			
			verifyInvariants();
			lock.unlock();
			for (unsigned int i = 0; i < count; i++) {
				actions[i].callback(actions[i].session, ExceptionPtr());
			}
		} else {
			vector<GetAction> actions;
			actions.reserve(getWaitlist.size());
			while (!getWaitlist.empty() && pqueue.top() != NULL && !pqueue.top()->atFullCapacity()) {
				GetAction action;
				action.callback = getWaitlist.front().callback;
				action.session  = newSession();
				getWaitlist.pop();
				actions.push_back(action);
			}
			
			verifyInvariants();
			lock.unlock();
			vector<GetAction>::const_iterator it, end = actions.end();
			for (it = actions.begin(); it != actions.end(); it++) {
				it->callback(it->session, ExceptionPtr());
			}
		}
	}
	
	void assignSessionsToGetWaiters(vector<Callback> &postLockActions) {
		while (!getWaitlist.empty() && pqueue.top() != NULL && !pqueue.top()->atFullCapacity()) {
			postLockActions.push_back(boost::bind(
				getWaitlist.front().callback, newSession(),
				ExceptionPtr()));
			getWaitlist.pop();
		}
	}
	
	void assignExceptionToGetWaiters(const ExceptionPtr &exception,
		vector<Callback> &postLockActions)
	{
		while (!getWaitlist.empty()) {
			postLockActions.push_back(boost::bind(
				getWaitlist.front().callback, SessionPtr(),
				exception));
			getWaitlist.pop();
		}
	}
	
public:
	Options options;
	string name;
	string secret;
	ComponentInfo componentInfo;
	
	/**
	 * All processes in this group are stored in 'processes'. 'pqueue'
	 * orders them according to usage() values from small to large.
	 *
	 * Invariant:
	 *    processes.empty() == (pqueue.top() == NULL)
	 *    processes.size() == count
	 *    if pqueue.top().atFullCapacity():
	 *       All processes are at full capacity.
	 */
	int count;
	ProcessList processes;
	PriorityQueue<Process> pqueue;
	
	/**
	 * get() requests for this group that cannot be immediately satisfied are
	 * put on this wait list, which must be processed as soon as the necessary
	 * resources have become free.
	 *
	 * Invariant 1:
	 *    if getWaitlist is non-empty:
	 *       processes.empty() or (all processes are at full capacity)
	 * Equivalently:
	 *    if !processes.empty() and (a process is not at full capacity):
	 *        getWaitlist is empty.
	 *
	 * Invariant 2:
	 *    if processes.empty() && !spawning():
	 *       getWaitlist is empty
	 * Equivalently:
	 *    if getWaitlist is non-empty:
	 *       !processes.empty() || spawning()
	 */
	queue<GetWaiter> getWaitlist;
	
	SpawnerPtr spawner;
	bool m_spawning;
	
	Group(const SuperGroupPtr &superGroup, const Options &options, const ComponentInfo &info);
	
	SessionPtr get(const Options &newOptions, const GetCallback &callback) {
		if (needsRestart(newOptions)) {
			restart(newOptions);
		} else {
			mergeOptions(newOptions);
		}
		if (shouldSpawn()) {
			spawn();
		}
		
		if (count == 0) {
			/* We don't have any processes yet, but it's on the way.
			 * Call the callback after a process has been spawned
			 * or has failed to spawn.
			 */
			assert(spawning());
			getWaitlist.push(GetWaiter(newOptions, callback));
			return SessionPtr();
		} else {
			Process *process = pqueue.top();
			assert(process != NULL);
			if (process->atFullCapacity()) {
				/* Looks like all processes are at full capacity.
				 * Wait until a new one has been spawned or until
				 * resources have become free.
				 */
				getWaitlist.push(GetWaiter(newOptions, callback));
				return SessionPtr();
			} else {
				return newSession();
			}
		}
	}
	
	// Thread-safe.
	SuperGroupPtr getSuperGroup() const {
		lock_guard<boost::mutex> lock(backrefSyncher);
		return superGroup.lock();
	}
	
	// Thread-safe.
	void setSuperGroup(const SuperGroupPtr &superGroup) {
		lock_guard<boost::mutex> lock(backrefSyncher);
		this->superGroup = superGroup;
	}
	
	// Thread-safe.
	PoolPtr getPool() const;
	
	// Thread-safe.
	bool detached() const {
		return getSuperGroup() == NULL;
	}
	
	void attach(const ProcessPtr &process) {
		process->setGroup(shared_from_this());
		processes.push_back(process);
		process->it = processes.last_iterator();
		process->pqHandle = pqueue.push(process.get(), process->usage());
		count++;
	}
	
	/**
	 * Detaches the given process from this Group. This function doesn't touch
	 * getWaitlist so be sure to fix its invariants afterwards if necessary.
	 */
	void detach(const ProcessPtr &process) {
		assert(process->group.lock().get() == this);
		assert(count > 0);
		process->setGroup(GroupPtr());
		processes.erase(process->it);
		pqueue.erase(process->pqHandle);
		count--;
	}
	
	/**
	 * Detaches all processes from this Group. This function doesn't touch
	 * getWaitlist so be sure to fix its invariants afterwards if necessary.
	 */
	void detachAll() {
		ProcessList::iterator it, end = processes.end();
		for (it = processes.begin(); it != end; it++) {
			(*it)->group.reset();
		}
		processes.clear();
		pqueue.clear();
		count = 0;
	}
	
	unsigned int usage() const {
		int result = count;
		if (spawning()) {
			result++;
		}
		return result;
	}
	
	bool garbageCollectable(unsigned long long now = 0) const {
		if (now == 0) {
			now = SystemTime::getUsec();
		}
		return usage() == 0
			&& getWaitlist.empty()
			&& options.getSpawnerTimeout() != 0
			&& now - spawner->lastUsed() >
				(unsigned long long) options.getSpawnerTimeout() * 1000000;
	}
	
	/** Whether a new process should be spawned for this group in case
	 * another get action is to be performed.
	 */
	bool shouldSpawn() const;
	
	/** Start spawning a new process in the background, in case this
	 * isn't already happening. Will ensure that at least options.minProcesses
	 * are started.
	 */
	void spawn() {
		if (!spawning()) {
			createInterruptableThread(
				boost::bind(&Group::spawnThreadMain,
					this, shared_from_this(), spawner,
					options.copyAndPersist().clearPerRequestFields()),
				"Group process spawner",
				1024 * 64);
			m_spawning = true;
		}
	}
	
	bool needsRestart(const Options &options) {
		struct stat buf;
		return cstat.stat(alwaysRestartFile, &buf, options.statThrottleRate) == 0 ||
		       fileChangeChecker.changed(restartFile, options.statThrottleRate);
	}
	
	void restart(const Options &options);
	
	bool spawning() const {
		return m_spawning;
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_GROUP_H_ */

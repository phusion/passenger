#ifndef _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_
#define _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_

#include <vector>
#include <ApplicationPool2/Common.h>
#include <ApplicationPool2/Options.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


typedef map<string, SuperGroupPtr> SuperGroupMap;

/**
 * Except for otherwise documented parts, this class is not thread-safe,
 * so only access within ApplicationPool lock.
 */
class SuperGroup: public enable_shared_from_this<SuperGroup> {
public:
	enum State {
		INITIALIZING,
		READY,
		RESTARTING
	}
	
	struct Component {
		GroupPtr group;
		bool     isDefault;
		
		/****************/
		/****************/
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
			options.persist();
		}
	};
	
	queue<GetAction> getWaitlist;
	
	vector<Component> loadSingleComponent(const Options &options) const {
		vector<Component> components;
		Component component;
		component.isDefault = true;
		component.group = make_shared<Group>(shared_from_this(), options);
		components.clear();
		components.push_back(component);
		return components;
	}
	
	Component *findDefaultComponent(const vector<Component> &components) const {
		vector<Component>::iterator it;
		
		for (it = components.begin(); it != components.end(); it++) {
			Component &component = *it;
			if (component->isDefault) {
				return &component;
			}
		}
		return NULL;
	}
	
	void adjustOptions(Options &options, const Component &component) const {
		// No-op.
	}
	
	bool routeMatch(const Component &component, const Options &options) const {
		return true;
	}
	
	static void detachComponents(const vector<Component> &components) {
		vector<Component>::iterator it, end = components.end();
		
		for (it = components.begin(); it != end; it++) {
			Component &component = *it;
			forceDetachGroup(component.group);
		}
	}
	
	static void forceDetachGroup(const GroupPtr &group) {
		group->detachAll();
		group->setSuperGroup(SuperGroupPtr());
	}
	
	/*********************/
	
	/*********************/
	
public:
	boost::mutex backrefSyncher;
	weak_ptr<Pool> pool;
	
	State state;
	vector<Component> components;
	Component *defaultComponent;
	string name;
	
	SuperGroup(const PoolPtr &pool, const Options &options) {
		this->pool = pool;
		state = INITIALIZING;
		restart(options);
	}
	
	~SuperGroup() {
		detachComponents(components);
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
	
	SessionPtr get(const Options &newOptions, const GetCallback &callback) {
		if (state == INITIALIZING) {
			getWaiters.push_back(GetAction(newOptions, callback));
		} else {
			if (needsRestart()) {
				restart(newOptions);
			}
			if (components.size() > 1) {
				const Component &component = route(newOptions);
				Options adjustedOptions = newOptions;
				adjustOptions(adjustedOptions, component);
				return component.group->get(adjustedOptions, callback);
			} else {
				return defaultComponent->group->get(newOptions, callback);
			}
		}
	}
	
	const Component &route(const Options &options) {
		return *defaultComponent;
	}
	
	unsigned int usage() const {
		vector<Component>::const_iterator it, end = components.end();
		unsigned int result = 0;
		
		for (it = components.begin(); it != end; it++) {
			result += it->usage();
		}
		return result;
	}
	
	void needsRestart() const {
		
	}
	
	void restart(const Options &options) {
		if (state != RESTARTING) {
			spawnInThread(reallyRestart, options.copy().persist());
		}
	}
	
	void restartThreadMain(Options options) {
		vector<Component> newComponents = loadSingleComponent(options);
		
		lock;
		detachComponents(components);
		components = newComponents;
		defaultComponent = findDefaultComponent(components);
		state = READY;
		process getWaiters;
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SUPER_GROUP_H_ */

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
#ifndef _PASSENGER_APPLICATION_POOL2_POOL_H_
#define _PASSENGER_APPLICATION_POOL2_POOL_H_

#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <sstream>
#include <iomanip>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <boost/foreach.hpp>
#include <boost/pool/object_pool.hpp>
// We use boost::container::vector instead of std::vector, because the
// former does not allocate memory in its default constructor. This is
// useful for post lock action vectors which often remain empty.
#include <boost/container/vector.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <oxt/dynamic_thread_group.hpp>
#include <oxt/backtrace.hpp>
#include <sys/types.h>
#include <MemoryKit/palloc.h>
#include <LoggingKit/LoggingKit.h>
#include <ConfigKit/ConfigKit.h>
#include <Exceptions.h>
#include <Hooks.h>
#include <SystemTools/SystemMetricsCollector.h>
#include <SystemTools/ProcessMetricsCollector.h>
#include <SystemTools/SystemTime.h>
#include <Utils/Lock.h>
#include <Utils/AnsiColorConstants.h>
#include <Utils/MessagePassing.h>
#include <Utils/VariantMap.h>
#include <Core/ApplicationPool/Common.h>
#include <Core/ApplicationPool/Context.h>
#include <Core/ApplicationPool/Process.h>
#include <Core/ApplicationPool/Group.h>
#include <Core/ApplicationPool/Session.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/SpawningKit/Factory.h>
#include <Shared/ApplicationPoolApiKey.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class Pool: public boost::enable_shared_from_this<Pool> {
public:
	struct AuthenticationOptions {
		uid_t uid;
		ApiKey apiKey;

		AuthenticationOptions()
			: uid(-1)
			{ }

		static AuthenticationOptions makeAuthorized() {
			AuthenticationOptions options;
			options.apiKey = ApiKey::makeSuper();
			return options;
		}
	};


	/****** Group data structure utilities ******/

	struct RestartOptions: public AuthenticationOptions {
		RestartMethod method;

		RestartOptions()
			: method(RM_DEFAULT)
			{ }

		static RestartOptions makeAuthorized() {
			RestartOptions options;
			options.apiKey = ApiKey::makeSuper();
			return options;
		}
	};

	/****** State inspection ******/

	struct InspectOptions: public AuthenticationOptions {
		bool colorize;
		bool verbose;

		InspectOptions()
			: colorize(false),
			  verbose(false)
			{ }

		InspectOptions(const VariantMap &options)
			: colorize(options.getBool("colorize", false, false)),
			  verbose(options.getBool("verbose", false, false))
			{ }

		InspectOptions(const Json::Value &options)
			: colorize(options.get("colorize", false).asBool()),
			  verbose(options.get("verbose", false).asBool())
			{ }

		static InspectOptions makeAuthorized() {
			InspectOptions options;
			options.apiKey = ApiKey::makeSuper();
			return options;
		}
	};

	struct ToXmlOptions: public AuthenticationOptions {
		bool secrets;

		ToXmlOptions()
			: secrets(true)
			{ }

		ToXmlOptions(const VariantMap &options)
			: secrets(options.getBool("secrets", false, false))
			{ }

		static ToXmlOptions makeAuthorized() {
			ToXmlOptions options;
			options.apiKey = ApiKey::makeSuper();
			return options;
		}
	};

	struct ToJsonOptions: public AuthenticationOptions {
		bool hasApplicationIdsFilter;
		StringKeyTable<bool> applicationIdsFilter;

		ToJsonOptions()
			: hasApplicationIdsFilter(false),
			  applicationIdsFilter(0, 0)
			{ }

		void set(const Json::Value &_options) {
			ConfigKit::Schema schema = createSchema();
			ConfigKit::Store options(schema, _options);

			if (!options["application_ids"].isNull()) {
				hasApplicationIdsFilter = true;
				applicationIdsFilter = StringKeyTable<bool>();

				const Json::Value subdoc = options["application_ids"];
				Json::Value::const_iterator it, end = subdoc.end();
				for (it = subdoc.begin(); it != end; it++) {
					applicationIdsFilter.insert(it->asString(), true);
				}
			}
		}

		static ConfigKit::Schema createSchema() {
			using namespace ConfigKit;
			ConfigKit::Schema schema;

			schema.add("application_ids", STRING_ARRAY_TYPE, OPTIONAL);

			schema.finalize();
			return schema;
		}

		static ToJsonOptions makeAuthorized() {
			ToJsonOptions options;
			options.apiKey = ApiKey::makeSuper();
			return options;
		}
	};


// Actually private, but marked public so that unit tests can access the fields.
public:
	friend class Group;
	friend class Process;
	friend struct tut::ApplicationPool2_PoolTest;

	mutable boost::mutex syncher;
	unsigned int max;
	unsigned long long maxIdleTime;
	bool selfchecking;

	Context *context;

	/**
	 * Code can register background threads in one of these dynamic thread groups
	 * to ensure that threads are interrupted and/or joined properly upon Pool
	 * destruction.
	 * All threads in 'interruptableThreads' will be interrupted and joined upon
	 * Pool destruction.
	 * All threads in 'nonInterruptableThreads' will be joined, but not interrupted,
	 * upon Pool destruction.
	 */
	dynamic_thread_group interruptableThreads;
	dynamic_thread_group nonInterruptableThreads;

	enum LifeStatus {
		ALIVE,
		PREPARED_FOR_SHUTDOWN,
		SHUTTING_DOWN,
		SHUT_DOWN
	} lifeStatus;

	mutable GroupMap groups;
	psg_pool_t *palloc;

	/**
	 * get() requests that...
	 * - cannot be immediately satisfied because the pool is at full
	 *   capacity and no existing processes can be killed,
	 * - and for which the super group isn't in the pool,
	 * ...are put on this wait list.
	 *
	 * This wait list is processed when one of the following things happen:
	 *
	 * - A process has been spawned but its associated group has
	 *   no get waiters. This process can be killed and the resulting
	 *   free capacity will be used to spawn a process for this
	 *   get request.
	 * - A process (that has apparently been spawned after getWaitlist
	 *   was populated) is done processing a request. This process can
	 *   then be killed to free capacity.
	 * - A process has failed to spawn, resulting in capacity to
	 *   become free.
	 * - A Group failed to initialize, resulting in free capacity.
	 * - Someone commanded Pool to detach a process, resulting in free
	 *   capacity.
	 * - Someone commanded Pool to detach a Group, resulting in
	 *   free capacity.
	 * - The 'max' option has been increased, resulting in free capacity.
	 *
	 * Invariant 1:
	 *    for all options in getWaitlist:
	 *       options.getAppGroupName() is not in 'groups'.
	 *
	 * Invariant 2:
	 *    if getWaitlist is non-empty:
	 *       atFullCapacity()
	 * Equivalently:
	 *    if !atFullCapacity():
	 *       getWaitlist is empty.
	 */
	vector<GetWaiter> getWaitlist;

// Actually private, but marked public so that unit tests can access the fields.
public:
	/****** Debugging support *******/

	struct DebugSupport {
		/** Mailbox for the unit tests to receive messages on. */
		MessageBoxPtr debugger;
		/** Mailbox for the ApplicationPool code to receive messages on. */
		MessageBoxPtr messages;

		// Choose aspects to debug.
		bool restarting;
		bool spawning;
		bool oobw;
		bool testOverflowRequestQueue;
		bool detachedProcessesChecker;

		// The following fields may only be accessed by Pool.
		boost::mutex syncher;
		unsigned int spawnLoopIteration;

		DebugSupport() {
			debugger = boost::make_shared<MessageBox>();
			messages = boost::make_shared<MessageBox>();
			restarting = true;
			spawning   = true;
			oobw       = false;
			detachedProcessesChecker = false;
			testOverflowRequestQueue = false;
			spawnLoopIteration = 0;
		}
	};

	typedef boost::shared_ptr<DebugSupport> DebugSupportPtr;

	DebugSupportPtr debugSupport;


	/****** Analytics collection ******/

	SystemMetricsCollector systemMetricsCollector;
	SystemMetrics systemMetrics;

	void initializeAnalyticsCollection();
	static void collectAnalytics(PoolPtr self);
	static void collectPids(const ProcessList &processes, vector<pid_t> &pids);
	static void updateProcessMetrics(const ProcessList &processes,
		const ProcessMetricMap &allMetrics,
		vector<ProcessPtr> &processesToDetach);
	void realCollectAnalytics();


	/****** Garbage collection ******/

	struct GarbageCollectorState {
		unsigned long long now;
		unsigned long long nextGcRunTime;
		boost::container::vector<Callback> actions;
	};

	boost::condition_variable garbageCollectionCond;

	void initializeGarbageCollection();
	static void garbageCollect(PoolPtr self);
	void maybeUpdateNextGcRuntime(GarbageCollectorState &state, unsigned long candidate);
	void checkWhetherProcessCanBeGarbageCollected(GarbageCollectorState &state,
		const GroupPtr &group, const ProcessPtr &process, ProcessList &output);
	void garbageCollectProcessesInGroup(GarbageCollectorState &state,
		const GroupPtr &group);
	void maybeCleanPreloader(GarbageCollectorState &state, const GroupPtr &group);
	unsigned long long realGarbageCollect();
	void wakeupGarbageCollector();


	/****** General utilities ******/

	static const char *maybeColorize(const InspectOptions &options, const char *color);
	static const char *maybePluralize(unsigned int count, const char *singular, const char *plural);
	static void runAllActions(const boost::container::vector<Callback> &actions);
	static void runAllActionsWithCopy(boost::container::vector<Callback> actions);
	bool runHookScripts(const char *name,
		const boost::function<void (HookScriptOptions &)> &setup) const;
	void verifyInvariants() const;
	void verifyExpensiveInvariants() const;
	void fullVerifyInvariants() const;
	void assignSessionsToGetWaiters(boost::container::vector<Callback> &postLockActions);
	template<typename Queue> static void assignExceptionToGetWaiters(Queue &getWaitlist,
		const ExceptionPtr &exception,
		boost::container::vector<Callback> &postLockActions);
	static void syncGetCallback(const AbstractSessionPtr &session, const ExceptionPtr &e,
		void *userData);


	/****** Group data structure utilities ******/

	struct DetachGroupWaitTicket {
		boost::mutex syncher;
		boost::condition_variable cond;
		bool done;

		DetachGroupWaitTicket() {
			done = false;
		}
	};

	const GroupPtr getGroup(const char *name);
	const pair<uid_t, gid_t> getGroupRunUidAndGids(const StaticString &appGroupName);
	Group *findMatchingGroup(const Options &options);
	GroupPtr createGroup(const Options &options);
	GroupPtr createGroupAndAsyncGetFromIt(const Options &options,
		const GetCallback &callback, boost::container::vector<Callback> &postLockActions);
	void forceDetachGroup(const GroupPtr &group,
		const Callback &callback,
		boost::container::vector<Callback> &postLockActions);
	static void syncDetachGroupCallback(boost::shared_ptr<DetachGroupWaitTicket> ticket);
	static void waitDetachGroupCallback(boost::shared_ptr<DetachGroupWaitTicket> ticket);


	/****** Process data structure utilities ******/

	struct DisableWaitTicket {
		boost::mutex syncher;
		boost::condition_variable cond;
		DisableResult result;
		bool done;

		DisableWaitTicket() {
			done = false;
		}
	};

	ProcessPtr findOldestIdleProcess(const Group *exclude = NULL) const;
	ProcessPtr findBestProcessToTrash() const;
	ProcessPtr forceFreeCapacity(const Group *exclude,
		boost::container::vector<Callback> &postLockActions);
	bool detachProcessUnlocked(const ProcessPtr &process,
		boost::container::vector<Callback> &postLockActions);
	static void syncDisableProcessCallback(const ProcessPtr &process, DisableResult result,
		boost::shared_ptr<DisableWaitTicket> ticket);
	void possiblySpawnMoreProcessesForExistingGroups();


	/****** State inspection ******/

	static Json::Value makeSingleValueJsonConfigFormat(const Json::Value &v,
		const Json::Value &defaultValue = Json::Value());
	static Json::Value makeSingleStrValueJsonConfigFormat(const StaticString &val);
	static Json::Value makeSingleStrValueJsonConfigFormat(const StaticString &val,
		const StaticString &defaultValue);
	static Json::Value makeSingleNonEmptyStrValueJsonConfigFormat(const StaticString &val);
	unsigned int capacityUsedUnlocked() const;
	bool atFullCapacityUnlocked() const;
	void inspectProcessList(const InspectOptions &options, stringstream &result,
		const Group *group, const ProcessList &processes) const;

public:
	typedef void (*AbortLongRunningConnectionsCallback)(const ProcessPtr &process);
	AbortLongRunningConnectionsCallback abortLongRunningConnectionsCallback;


	/****** Initialization and shutdown ******/

	Pool(Context *context);
	~Pool();
	void initialize();
	void initDebugging();
	void prepareForShutdown();
	void destroy();


	/****** General utilities ******/

	Context *getContext();
	SpawningKit::Context *getSpawningKitContext() const;
	const RandomGeneratorPtr &getRandomGenerator() const;


	/****** Group manipulation ******/

	GroupPtr findOrCreateGroup(const Options &options);
	GroupPtr findGroupByApiKey(const StaticString &value, bool lock = true) const;
	bool detachGroupByName(const HashedStaticString &name);
	bool detachGroupByApiKey(const StaticString &value);
	bool restartGroupByName(const StaticString &name,
		const RestartOptions &options = RestartOptions::makeAuthorized());
	unsigned int restartGroupsByAppRoot(const StaticString &appRoot,
		const RestartOptions &options = RestartOptions::makeAuthorized());


	/***** Process manipulation ******/

	vector<ProcessPtr> getProcesses(bool lock = true) const;
	ProcessPtr findProcessByGupid(const StaticString &gupid, bool lock = true) const;
	ProcessPtr findProcessByPid(pid_t pid, bool lock = true) const;
	bool detachProcess(const ProcessPtr &process);
	bool detachProcess(pid_t pid,
		const AuthenticationOptions &options = AuthenticationOptions::makeAuthorized());
	bool detachProcess(const string &gupid,
		const AuthenticationOptions &options = AuthenticationOptions::makeAuthorized());
	DisableResult disableProcess(const StaticString &gupid);


	/****** State inspection ******/

	unsigned int capacityUsed() const;
	bool atFullCapacity() const;
	unsigned int getProcessCount(bool lock = true) const;
	unsigned int getGroupCount() const;
	string inspect(const InspectOptions &options = InspectOptions::makeAuthorized(),
		bool lock = true) const;
	string toXml(const ToXmlOptions &options = ToXmlOptions::makeAuthorized(),
		bool lock = true) const;
	Json::Value inspectPropertiesInAdminPanelFormat(const ToJsonOptions &options = ToJsonOptions::makeAuthorized()) const;
	Json::Value inspectConfigInAdminPanelFormat(const ToJsonOptions &options = ToJsonOptions::makeAuthorized()) const;


	/****** Miscellaneous ******/

	void asyncGet(const Options &options, const GetCallback &callback, bool lockNow = true);
	SessionPtr get(const Options &options, Ticket *ticket);
	void setMax(unsigned int max);
	void setMaxIdleTime(unsigned long long value);
	void enableSelfChecking(bool enabled);
	bool isSpawning(bool lock = true) const;
	bool authorizeByApiKey(const ApiKey &key, bool lock = true) const;
	bool authorizeByUid(uid_t uid, bool lock = true) const;
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_POOL_H_ */

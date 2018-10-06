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
#ifndef _PASSENGER_APPLICATION_POOL_PROCESS_H_
#define _PASSENGER_APPLICATION_POOL_PROCESS_H_

#include <string>
#include <vector>
#include <algorithm>
#include <boost/intrusive_ptr.hpp>
#include <boost/move/core.hpp>
#include <boost/container/vector.hpp>
#include <oxt/system_calls.hpp>
#include <oxt/spin_lock.hpp>
#include <oxt/macros.hpp>
#include <sys/types.h>
#include <cstdio>
#include <climits>
#include <cassert>
#include <cstring>
#include <Constants.h>
#include <FileDescriptor.h>
#include <LoggingKit/LoggingKit.h>
#include <SystemTools/ProcessMetricsCollector.h>
#include <SystemTools/SystemTime.h>
#include <StrIntTools/StrIntUtils.h>
#include <Utils/Lock.h>
#include <Core/ApplicationPool/Common.h>
#include <Core/ApplicationPool/Socket.h>
#include <Core/ApplicationPool/Session.h>
#include <Core/SpawningKit/PipeWatcher.h>
#include <Core/SpawningKit/Result.h>
#include <Shared/ApplicationPoolApiKey.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


typedef boost::container::vector<ProcessPtr> ProcessList;

/**
 * Represents an application process, as spawned by a SpawningKit::Spawner. Every
 * Process has a PID, a stdin pipe, an output pipe and a list of sockets on which
 * it listens for connections. A Process object is contained inside a Group.
 *
 * The stdin pipe is mapped to the process's STDIN and is used for garbage
 * collection: closing the STDIN part causes the process to gracefully terminate itself.
 *
 * The output pipe is mapped to the process' STDOUT and STDERR. All data coming
 * from those pipes will be printed.
 *
 * Except for the otherwise documented parts, this class is not thread-safe,
 * so only use within the Pool lock.
 *
 * ## Normal usage
 *
 *  1. Create a session with newSession().
 *  2. Initiate the session by calling initiate() on it.
 *  3. Perform I/O through session->fd().
 *  4. When done, close the session by calling close() on it.
 *  5. Call process.sessionClosed().
 *
 * ## Life time
 *
 * A Process object lives until the containing Group calls `detach(process)`,
 * which indicates that it wants this Process to shut down. The Process object
 * is stored in the `detachedProcesses` collection in the Group and is no longer
 * eligible for receiving requests. Once all requests on this Process have finished,
 * `triggerShutdown()` will be called, which will send a message to the
 * OS process telling it to shut down. Once the OS process is gone, `cleanup()` is
 * called, and the Process object is removed from the collection.
 *
 * This means that a Group outlives all its Processes, a Process outlives all
 * its Sessions, and a Process also outlives the OS process.
 */
class Process {
public:
	static const unsigned int MAX_SOCKETS_ACCEPTING_HTTP_REQUESTS = 3;

private:
	/*************************************************************
	 * Read-only fields, set once during initialization and never
	 * written to again. Reading is thread-safe.
	 *************************************************************/

	BasicProcessInfo info;
	DynamicBuffer stringBuffer;
	SocketList sockets;

	/**
	 * The maximum amount of concurrent sessions this process can handle.
	 * 0 means unlimited. Automatically inferred from the sockets.
	 */
	int concurrency;

	/**
	 * A subset of 'sockets': all sockets that accept HTTP requests
	 * from the Passenger Core controller.
	 */
	unsigned int socketsAcceptingHttpRequestsCount;
	Socket *socketsAcceptingHttpRequests[MAX_SOCKETS_ACCEPTING_HTTP_REQUESTS];

	/** Input pipe. See Process class description. */
	FileDescriptor inputPipe;

	/**
	 * Pipe on which this process outputs stdout and stderr data. Mapped to the
	 * process's STDOUT and STDERR.
	 */
	FileDescriptor outputPipe;

	/**
	 * The code revision of the application, inferred through various means.
	 * See Spawner::prepareSpawn() to learn how this is determined.
	 * May be an empty string if no code revision has been inferred.
	 */
	StaticString codeRevision;

	/**
	 * Time at which the Spawner that created this process was created.
	 * Microseconds resolution.
	 */
	unsigned long long spawnerCreationTime;

	/** Time at which we started spawning this process. Microseconds resolution. */
	unsigned long long spawnStartTime;

	/**
	 * Time at which we finished spawning this process, i.e. when this
	 * process was finished initializing. Microseconds resolution.
	 */
	unsigned long long spawnEndTime;

	SpawningKit::Result::Type type;

	/**
	 * Whether it is required that triggerShutdown() and cleanup() must be called
	 * before destroying this Process. Normally true, except for dummy Process
	 * objects created by Pool::asyncGet() with options.noop == true, because those
	 * processes are never added to Group.enabledProcesses.
	 */
	bool requiresShutdown;


	/*************************************************************
	 * Read-write fields.
	 *************************************************************/

	mutable boost::atomic<int> refcount;

	/** A mutex to protect access to `lifeStatus`. */
	mutable oxt::spin_lock lifetimeSyncher;

	/** The index inside the associated Group's process list. */
	unsigned int index;


	/*************************************************************
	 * Methods
	 *************************************************************/

	/****** Initialization and destruction ******/

	struct InitializationLog {
		struct String {
			unsigned int offset;
			unsigned int size;
		};

		struct SocketStringOffsets {
			String address;
			String protocol;
			String description;
		};

		vector<SocketStringOffsets> socketStringOffsets;
		String codeRevision;
	};

	void appendJsonFieldToBuffer(std::string &buffer, const Json::Value &json,
		const char *key, InitializationLog::String &str, bool required = true) const
	{
		StaticString value;
		if (required) {
			value = getJsonStaticStringField(json, key);
		} else {
			value = getJsonStaticStringField(json, Json::StaticString(key),
				StaticString());
		}
		str.offset = buffer.size();
		str.size   = value.size();
		buffer.append(value.data(), value.size());
		buffer.append(1, '\0');
	}

	void initializeSocketsAndStringFields(const SpawningKit::Result &result) {
		Json::Value doc, sockets(Json::arrayValue);
		vector<SpawningKit::Result::Socket>::const_iterator it, end = result.sockets.end();

		for (it = result.sockets.begin(); it != end; it++) {
			sockets.append(it->inspectAsJson());
		}

		doc["sockets"] = sockets;
		initializeSocketsAndStringFields(doc);
	}

	void initializeSocketsAndStringFields(const Json::Value &json) {
		InitializationLog log;
		string buffer;


		// Step 1: append strings to temporary buffer and take note of their
		// offsets within the temporary buffer.

		Json::Value sockets = getJsonField(json, "sockets");
		// The const_cast here works around a jsoncpp bug.
		Json::Value::const_iterator it = const_cast<const Json::Value &>(sockets).begin();
		Json::Value::const_iterator end = const_cast<const Json::Value &>(sockets).end();
		buffer.reserve(1024);

		for (it = sockets.begin(); it != end; it++) {
			const Json::Value &socket = *it;
			InitializationLog::SocketStringOffsets offsets;

			appendJsonFieldToBuffer(buffer, socket, "address", offsets.address);
			appendJsonFieldToBuffer(buffer, socket, "protocol", offsets.protocol);
			appendJsonFieldToBuffer(buffer, socket, "description", offsets.description,
				false);

			log.socketStringOffsets.push_back(offsets);
		}

		if (json.isMember("code_revision")) {
			appendJsonFieldToBuffer(buffer, json, "code_revision", log.codeRevision);
		}


		// Step 2: allocate the real buffer.

		this->stringBuffer = DynamicBuffer(buffer.size());
		memcpy(this->stringBuffer.data, buffer.data(), buffer.size());


		// Step 3: initialize the string fields and point them to
		// addresses within the real buffer.

		unsigned int i;
		const char *base = this->stringBuffer.data;

		it = const_cast<const Json::Value &>(sockets).begin();
		for (i = 0; it != end; it++, i++) {
			const Json::Value &socket = *it;
			this->sockets.add(
				info.pid,
				StaticString(base + log.socketStringOffsets[i].address.offset,
					log.socketStringOffsets[i].address.size),
				StaticString(base + log.socketStringOffsets[i].protocol.offset,
					log.socketStringOffsets[i].protocol.size),
				StaticString(base + log.socketStringOffsets[i].description.offset,
					log.socketStringOffsets[i].description.size),
				getJsonIntField(socket, "concurrency"),
				getJsonBoolField(socket, "accept_http_requests")
			);
		}

		if (json.isMember("code_revision")) {
			codeRevision = StaticString(base + log.codeRevision.offset,
				log.codeRevision.size);
		}
	}

	void indexSocketsAcceptingHttpRequests() {
		SocketList::iterator it;

		concurrency = 0;
		memset(socketsAcceptingHttpRequests, 0, sizeof(socketsAcceptingHttpRequests));

		for (it = sockets.begin(); it != sockets.end(); it++) {
			Socket *socket = &(*it);
			if (!socket->acceptHttpRequests) {
				continue;
			}
			if (socketsAcceptingHttpRequestsCount == MAX_SOCKETS_ACCEPTING_HTTP_REQUESTS) {
				throw RuntimeException("The process has too many sockets that accept HTTP requests. "
					"A maximum of " + toString(MAX_SOCKETS_ACCEPTING_HTTP_REQUESTS) + " is allowed");
			}

			socketsAcceptingHttpRequests[socketsAcceptingHttpRequestsCount] = socket;
			socketsAcceptingHttpRequestsCount++;

			if (concurrency >= 0) {
				if (socket->concurrency < 0) {
					// If one of the sockets has a concurrency of
					// < 0 (unknown) then we mark this entire Process
					// as having a concurrency of -1 (unknown).
					concurrency = -1;
				} else if (socket->concurrency == 0) {
					// If one of the sockets has a concurrency of
					// 0 (unlimited) then we mark this entire Process
					// as having a concurrency of 0.
					concurrency = -999;
				} else {
					concurrency += socket->concurrency;
				}
			}
		}

		if (concurrency == -999) {
			concurrency = 0;
		}
	}

	void destroySelf() const {
		this->~Process();
		LockGuard l(getContext()->memoryManagementSyncher);
		getContext()->processObjectPool.free(const_cast<Process *>(this));
	}


	/****** Miscellaneous ******/

	static bool isZombie(pid_t pid) {
		string filename = "/proc/" + toString(pid) + "/status";
		FILE *f = fopen(filename.c_str(), "r");
		if (f == NULL) {
			// Don't know.
			return false;
		}

		bool result = false;
		while (!feof(f)) {
			char buf[512];
			const char *line;

			line = fgets(buf, sizeof(buf), f);
			if (line == NULL) {
				break;
			}
			if (strcmp(line, "State:	Z (zombie)\n") == 0) {
				// Is a zombie.
				result = true;
				break;
			}
		}
		fclose(f);
		return result;
	}

	string getAppGroupName(const BasicGroupInfo *info) const;
	string getAppLogFile(const BasicGroupInfo *info) const;

public:
	/*************************************************************
	 * Information used by Pool. Do not write to these from
	 * outside the Pool. If you read these make sure the Pool
	 * isn't concurrently modifying.
	 *************************************************************/

	/** Last time when a session was opened for this Process. */
	unsigned long long lastUsed;
	/** Number of sessions currently open.
	 * @invariant session >= 0
	 */
	int sessions;
	/** Number of sessions opened so far. */
	unsigned int processed;
	/** Do not access directly, always use `isAlive()`/`isDead()`/`getLifeStatus()` or
	 * through `lifetimeSyncher`. */
	enum LifeStatus {
		/** Up and operational. */
		ALIVE,
		/** This process has been detached, and the detached processes checker has
		 * verified that there are no active sessions left and has told the process
		 * to shut down. In this state we're supposed to wait until the process
		 * has actually shutdown, after which cleanup() must be called. */
		SHUTDOWN_TRIGGERED,
		/**
		 * The process has exited and cleanup() has been called. In this state,
		 * this object is no longer usable.
		 */
		DEAD
	} lifeStatus;
	enum EnabledStatus {
		/** Up and operational. */
		ENABLED,
		/** Process is being disabled. The containing Group is waiting for
		 * all sessions on this Process to finish. It may in some corner
		 * cases still be selected for processing requests.
		 */
		DISABLING,
		/** Process is fully disabled and should not be handling any
		 * requests. It *may* still handle some requests, e.g. by
		 * the Out-of-Band-Work trigger.
		 */
		DISABLED,
		/**
		 * Process has been detached. It will be removed from the Group
		 * as soon we have detected that the OS process has exited. Detached
		 * processes are allowed to finish their requests, but are not
		 * eligible for new requests.
		 */
		DETACHED
	} enabled;
	enum OobwStatus {
		/** Process is not using out-of-band work. */
		OOBW_NOT_ACTIVE,
		/** The process has requested out-of-band work. At some point, the code
		 * will see this and set the status to OOBW_IN_PROGRESS. */
		OOBW_REQUESTED,
		/** An out-of-band work is in progress. We need to wait until all
		 * sessions have ended and the process has been disabled before the
		 * out-of-band work can be performed. */
		OOBW_IN_PROGRESS,
	} oobwStatus;
	/** Caches whether or not the OS process still exists. */
	mutable bool m_osProcessExists: 1;
	bool longRunningConnectionsAborted: 1;
	/** Time at which shutdown began. */
	time_t shutdownStartTime;
	/** Collected by Pool::collectAnalytics(). */
	ProcessMetrics metrics;


	Process(const BasicGroupInfo *groupInfo, const Json::Value &args)
		: info(this, groupInfo, args),
		  socketsAcceptingHttpRequestsCount(0),
		  spawnerCreationTime(getJsonUint64Field(args, "spawner_creation_time")),
		  spawnStartTime(getJsonUint64Field(args, "spawn_start_time")),
		  spawnEndTime(SystemTime::getUsec()),
		  type(args["type"] == "dummy" ? SpawningKit::Result::DUMMY : SpawningKit::Result::UNKNOWN),
		  requiresShutdown(false),
		  refcount(1),
		  index(-1),
		  lastUsed(spawnEndTime),
		  sessions(0),
		  processed(0),
		  lifeStatus(ALIVE),
		  enabled(ENABLED),
		  oobwStatus(OOBW_NOT_ACTIVE),
		  m_osProcessExists(true),
		  longRunningConnectionsAborted(false),
		  shutdownStartTime(0)
	{
		initializeSocketsAndStringFields(args);
		indexSocketsAcceptingHttpRequests();
	}

	Process(const BasicGroupInfo *groupInfo, const SpawningKit::Result &skResult,
		const Json::Value &args)
		: info(this, groupInfo, skResult),
		  socketsAcceptingHttpRequestsCount(0),
		  spawnerCreationTime(getJsonUint64Field(args, "spawner_creation_time")),
		  spawnStartTime(skResult.spawnStartTime),
		  spawnEndTime(skResult.spawnEndTime),
		  type(skResult.type),
		  requiresShutdown(false),
		  refcount(1),
		  index(-1),
		  lastUsed(spawnEndTime),
		  sessions(0),
		  processed(0),
		  lifeStatus(ALIVE),
		  enabled(ENABLED),
		  oobwStatus(OOBW_NOT_ACTIVE),
		  m_osProcessExists(true),
		  longRunningConnectionsAborted(false),
		  shutdownStartTime(0)
	{
		initializeSocketsAndStringFields(skResult);
		indexSocketsAcceptingHttpRequests();

		inputPipe = skResult.stdinFd;
		outputPipe = skResult.stdoutAndErrFd;

		if (outputPipe != -1) {
			SpawningKit::PipeWatcherPtr watcher = boost::make_shared<SpawningKit::PipeWatcher>(
				outputPipe, "output", getAppGroupName(groupInfo),
				getAppLogFile(groupInfo), skResult.pid);
			if (!args["log_file"].isNull()) {
				watcher->setLogFile(args["log_file"].asString());
			}
			watcher->initialize();
			watcher->start();
		}
	}

	~Process() {
		if (OXT_UNLIKELY(requiresShutdown && !isDead())) {
			P_BUG("You must call Process::triggerShutdown() and Process::cleanup() before actually "
				"destroying the Process object.");
		}
	}

	void initializeStickySessionId(unsigned int value) {
		info.stickySessionId = value;
	}

	void forceMaxConcurrency(int value) {
		assert(value >= 0);
		concurrency = value;
		for (unsigned i = 0; i < socketsAcceptingHttpRequestsCount; i++) {
			socketsAcceptingHttpRequests[i]->concurrency = concurrency;
		}
	}

	void shutdownNotRequired() {
		requiresShutdown = false;
	}


	/****** Memory and life time management ******/

	void ref() const {
		refcount.fetch_add(1, boost::memory_order_relaxed);
	}

	void unref() const {
		if (refcount.fetch_sub(1, boost::memory_order_release) == 1) {
			boost::atomic_thread_fence(boost::memory_order_acquire);
			destroySelf();
		}
	}

	ProcessPtr shared_from_this() {
		return ProcessPtr(this);
	}

	static void forceTriggerShutdownAndCleanup(ProcessPtr process) {
		if (process != NULL) {
			process->triggerShutdown();
			// Pretend like the OS process has exited so
			// that the canCleanup() precondition is true.
			process->m_osProcessExists = false;
			process->cleanup();
		}
	}

	// Thread-safe.
	bool isAlive() const {
		oxt::spin_lock::scoped_lock lock(lifetimeSyncher);
		return lifeStatus == ALIVE;
	}

	// Thread-safe.
	bool hasTriggeredShutdown() const {
		oxt::spin_lock::scoped_lock lock(lifetimeSyncher);
		return lifeStatus == SHUTDOWN_TRIGGERED;
	}

	// Thread-safe.
	bool isDead() const {
		oxt::spin_lock::scoped_lock lock(lifetimeSyncher);
		return lifeStatus == DEAD;
	}

	// Thread-safe.
	LifeStatus getLifeStatus() const {
		oxt::spin_lock::scoped_lock lock(lifetimeSyncher);
		return lifeStatus;
	}

	bool canTriggerShutdown() const {
		return getLifeStatus() == ALIVE && sessions == 0;
	}

	void triggerShutdown() {
		assert(canTriggerShutdown());
		{
			time_t now = SystemTime::get();
			oxt::spin_lock::scoped_lock lock(lifetimeSyncher);
			assert(lifeStatus == ALIVE);
			lifeStatus = SHUTDOWN_TRIGGERED;
			shutdownStartTime = now;
		}
		if (inputPipe != -1) {
			inputPipe.close();
		}
	}

	bool shutdownTimeoutExpired() const {
		return SystemTime::get() >= shutdownStartTime + PROCESS_SHUTDOWN_TIMEOUT;
	}

	bool canCleanup() const {
		return getLifeStatus() == SHUTDOWN_TRIGGERED && !osProcessExists();
	}

	void cleanup() {
		assert(canCleanup());

		P_TRACE(2, "Cleaning up process " << inspect());
		if (type != SpawningKit::Result::DUMMY) {
			SocketList::iterator it, end = sockets.end();
			for (it = sockets.begin(); it != end; it++) {
				if (getSocketAddressType(it->address) == SAT_UNIX) {
					string filename = parseUnixSocketAddress(it->address);
					syscalls::unlink(filename.c_str());
				}
				it->closeAllConnections();
			}
		}

		oxt::spin_lock::scoped_lock lock(lifetimeSyncher);
		lifeStatus = DEAD;
	}


	/****** Basic information queries ******/

	OXT_FORCE_INLINE
	Context *getContext() const {
		return info.groupInfo->context;
	}

	Group *getGroup() const {
		return info.groupInfo->group;
	}

	StaticString getGroupName() const {
		return info.groupInfo->name;
	}

	const ApiKey &getApiKey() const {
		return info.groupInfo->apiKey;
	}

	const BasicProcessInfo &getInfo() const {
		return info;
	}

	pid_t getPid() const {
		return info.pid;
	}

	StaticString getGupid() const {
		return StaticString(info.gupid, info.gupidSize);
	}

	unsigned int getStickySessionId() const {
		return info.stickySessionId;
	}

	unsigned long long getSpawnerCreationTime() const {
		return spawnerCreationTime;
	}

	bool isDummy() const {
		return type == SpawningKit::Result::DUMMY;
	}


	/****** Miscellaneous ******/

	unsigned int getIndex() const {
		return index;
	}

	void setIndex(unsigned int i) {
		index = i;
	}

	const SocketList &getSockets() const {
		return sockets;
	}

	Socket *findSocketsAcceptingHttpRequestsAndWithLowestBusyness() const {
		if (OXT_UNLIKELY(socketsAcceptingHttpRequestsCount == 0)) {
			return NULL;
		} else if (socketsAcceptingHttpRequestsCount == 1) {
			return socketsAcceptingHttpRequests[0];
		} else {
			int leastBusySocketIndex = 0;
			int lowestBusyness = socketsAcceptingHttpRequests[0]->busyness();

			for (unsigned i = 1; i < socketsAcceptingHttpRequestsCount; i++) {
				if (socketsAcceptingHttpRequests[i]->busyness() < lowestBusyness) {
					leastBusySocketIndex = i;
					lowestBusyness = socketsAcceptingHttpRequests[i]->busyness();
				}
			}

			return socketsAcceptingHttpRequests[leastBusySocketIndex];
		}
	}

	/** Checks whether the OS process exists.
	 * Once it has been detected that it doesn't, that event is remembered
	 * so that we don't accidentally ping any new processes that have the
	 * same PID.
	 */
	bool osProcessExists() const {
		if (type != SpawningKit::Result::DUMMY && m_osProcessExists) {
			if (syscalls::kill(getPid(), 0) == 0) {
				/* On some environments, e.g. Heroku, the init process does
				 * not properly reap adopted zombie processes, which can interfere
				 * with our process existance check. To work around this, we
				 * explicitly check whether or not the process has become a zombie.
				 */
				m_osProcessExists = !isZombie(getPid());
			} else {
				m_osProcessExists = errno != ESRCH;
			}
			return m_osProcessExists;
		} else {
			return false;
		}
	}

	/** Kill the OS process with the given signal. */
	int kill(int signo) {
		if (osProcessExists()) {
			return syscalls::kill(getPid(), signo);
		} else {
			return 0;
		}
	}

	int busyness() const {
		/* Different processes within a Group may have different
		 * 'concurrency' values. We want:
		 * - the process with the smallest busyness to be be picked for routing.
		 * - to give processes with concurrency == 0 or -1 more priority (in general)
		 *   over processes with concurrency > 0.
		 * Therefore, in case of processes with concurrency > 0, we describe our
		 * busyness as a percentage of 'concurrency', with the percentage value
		 * in [0..INT_MAX] instead of [0..1]. That way, the busyness value
		 * of processes with concurrency > 0 is usually higher than that of processes
		 * with concurrency == 0 or -1.
		 */
		if (concurrency <= 0) {
			return sessions;
		} else {
			return (int) (((long long) sessions * INT_MAX) / (double) concurrency);
		}
	}

	/**
	 * Whether we've reached the maximum number of concurrent sessions for this
	 * process.
	 */
	bool isTotallyBusy() const {
		return concurrency > 0 && sessions >= concurrency;
	}

	/**
	 * Whether a get() request can be routed to this process, assuming that
	 * the sticky session ID (if any) matches. This is only not the case
	 * if this process is totally busy.
	 */
	bool canBeRoutedTo() const {
		return !isTotallyBusy();
	}

	/**
	 * Create a new communication session with this process. This will connect to one
	 * of the session sockets or reuse an existing connection. See Session for
	 * more information about sessions.
	 *
	 * If you know the current time (in microseconds), pass it to `now`, which
	 * prevents this function from having to query the time.
	 *
	 * You SHOULD call sessionClosed() when one's done with the session.
	 * Failure to do so will mess up internal statistics but will otherwise
	 * not result in any harmful behavior.
	 */
	SessionPtr newSession(unsigned long long now = 0) {
		Socket *socket = findSocketsAcceptingHttpRequestsAndWithLowestBusyness();
		if (socket->isTotallyBusy()) {
			return SessionPtr();
		} else {
			socket->sessions++;
			this->sessions++;
			if (now != 0) {
				lastUsed = now;
			} else {
				lastUsed = SystemTime::getUsec();
			}
			return createSessionObject(socket);
		}
	}

	SessionPtr createSessionObject(Socket *socket) {
		struct Guard {
			Context *context;
			Session *session;

			Guard(Context *c, Session *s)
				: context(c),
				  session(s)
				{ }

			~Guard() {
				if (session != NULL) {
					context->sessionObjectPool.free(session);
				}
			}

			void clear() {
				session = NULL;
			}
		};

		Context *context = getContext();
		LockGuard l(context->memoryManagementSyncher);
		Session *session = context->sessionObjectPool.malloc();
		Guard guard(context, session);
		session = new (session) Session(context, &info, socket);
		guard.clear();
		return SessionPtr(session, false);
	}

	void sessionClosed(Session *session) {
		Socket *socket = session->getSocket();

		assert(socket->sessions > 0);
		assert(sessions > 0);

		socket->sessions--;
		this->sessions--;
		processed++;
		assert(!isTotallyBusy());
	}

	/**
	 * Returns the uptime of this process so far, as a string.
	 */
	string uptime() const {
		return distanceOfTimeInWords(spawnEndTime / 1000000);
	}

	string inspect() const {
		assert(getLifeStatus() != DEAD);
		stringstream result;
		result << "(pid=" << getPid() << ", group=" << getGroupName() << ")";
		return result.str();
	}

	template<typename Stream>
	void inspectXml(Stream &stream, bool includeSockets = true) const {
		stream << "<pid>" << getPid() << "</pid>";
		stream << "<sticky_session_id>" << getStickySessionId() << "</sticky_session_id>";
		stream << "<gupid>" << getGupid() << "</gupid>";
		stream << "<concurrency>" << concurrency << "</concurrency>";
		stream << "<sessions>" << sessions << "</sessions>";
		stream << "<busyness>" << busyness() << "</busyness>";
		stream << "<processed>" << processed << "</processed>";
		stream << "<spawner_creation_time>" << spawnerCreationTime << "</spawner_creation_time>";
		stream << "<spawn_start_time>" << spawnStartTime << "</spawn_start_time>";
		stream << "<spawn_end_time>" << spawnEndTime << "</spawn_end_time>";
		stream << "<last_used>" << lastUsed << "</last_used>";
		stream << "<last_used_desc>" << distanceOfTimeInWords(lastUsed / 1000000).c_str() << " ago</last_used_desc>";
		stream << "<uptime>" << uptime() << "</uptime>";
		if (!codeRevision.empty()) {
			stream << "<code_revision>" << escapeForXml(codeRevision) << "</code_revision>";
		}
		switch (lifeStatus) {
		case ALIVE:
			stream << "<life_status>ALIVE</life_status>";
			break;
		case SHUTDOWN_TRIGGERED:
			stream << "<life_status>SHUTDOWN_TRIGGERED</life_status>";
			break;
		case DEAD:
			stream << "<life_status>DEAD</life_status>";
			break;
		default:
			P_BUG("Unknown 'lifeStatus' state " << (int) lifeStatus);
		}
		switch (enabled) {
		case ENABLED:
			stream << "<enabled>ENABLED</enabled>";
			break;
		case DISABLING:
			stream << "<enabled>DISABLING</enabled>";
			break;
		case DISABLED:
			stream << "<enabled>DISABLED</enabled>";
			break;
		case DETACHED:
			stream << "<enabled>DETACHED</enabled>";
			break;
		default:
			P_BUG("Unknown 'enabled' state " << (int) enabled);
		}
		if (metrics.isValid()) {
			stream << "<has_metrics>true</has_metrics>";
			stream << "<cpu>" << (int) metrics.cpu << "</cpu>";
			stream << "<rss>" << metrics.rss << "</rss>";
			stream << "<pss>" << metrics.pss << "</pss>";
			stream << "<private_dirty>" << metrics.privateDirty << "</private_dirty>";
			stream << "<swap>" << metrics.swap << "</swap>";
			stream << "<real_memory>" << metrics.realMemory() << "</real_memory>";
			stream << "<vmsize>" << metrics.vmsize << "</vmsize>";
			stream << "<process_group_id>" << metrics.processGroupId << "</process_group_id>";
			stream << "<command>" << escapeForXml(metrics.command) << "</command>";
		}
		if (includeSockets) {
			SocketList::const_iterator it;

			stream << "<sockets>";
			for (it = sockets.begin(); it != sockets.end(); it++) {
				const Socket &socket = *it;
				stream << "<socket>";
				stream << "<address>" << escapeForXml(socket.address) << "</address>";
				stream << "<protocol>" << escapeForXml(socket.protocol) << "</protocol>";
				if (!socket.description.empty()) {
					stream << "<description>" << escapeForXml(socket.description) << "</description>";
				}
				stream << "<concurrency>" << socket.concurrency << "</concurrency>";
				stream << "<accept_http_requests>" << socket.acceptHttpRequests << "</accept_http_requests>";
				stream << "<sessions>" << socket.sessions << "</sessions>";
				stream << "</socket>";
			}
			stream << "</sockets>";
		}
	}
};


inline void
intrusive_ptr_add_ref(const Process *process) {
	process->ref();
}

inline void
intrusive_ptr_release(const Process *process) {
	process->unref();
}


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_PROCESS_H_ */

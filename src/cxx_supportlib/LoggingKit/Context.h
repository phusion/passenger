/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_LOGGING_KIT_CONTEXT_H_
#define _PASSENGER_LOGGING_KIT_CONTEXT_H_

#include <queue>

#include <jsoncpp/json.h>
#include <oxt/macros.hpp>
#include <oxt/thread.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/thread.hpp>
#include <boost/atomic.hpp>
#include <ConfigKit/ConfigKit.h>
#include <LoggingKit/Forward.h>
#include <LoggingKit/Config.h>
#include <SystemTools/SystemTime.h>
#include <DataStructures/StringKeyTable.h>

namespace Passenger {
namespace LoggingKit {

using namespace std;
using namespace oxt;


/**
 * Note about file descriptor handling:
 * the "target" and "file_descriptor_log_target" config options
 * accept an "fd" suboption to force LoggingKit to use a specific
 * file descriptor. LoggingKit will take over ownership of this fd,
 * but only in the following circumstances:
 *
 * - If you pass this "fd" option to the Context constructor, then
 *   LoggingKit takes ownership only when the constructor
 *   succeeds.
 * - If you pass this "fd" option to `prepareConfigChange()`, then
 *   LoggingKit takes ownership only when `commitConfigChange()`
 *   returns.
 *
 * If anything goes wrong in the constructor, or if
 * `commitConfigChange()` is never called, then the caller is
 * responsible for cleaning up the fd.
 */
class Context {
public:
	typedef LoggingKit::ConfigChangeRequest ConfigChangeRequest;

private:
	Schema schema;
	mutable boost::mutex syncher;
	ConfigKit::Store config;
	boost::atomic<ConfigRealization *> configRlz;

	mutable boost::mutex gcSyncher;
	oxt::thread *gcThread;
	boost::condition_variable gcShuttingDownCond, gcHasShutDownCond;
	queue< pair<ConfigRealization *, MonotonicTimeUsec> > oldConfigs;
	bool shuttingDown;

	struct TimestampedLog {
		// time at which time the log entered the core, which is unfortunately somewhat
		// arbitrarily later than that it was logged in the user program
		unsigned long long timestamp;
		string sourceId;
		string lineText;
	};
	typedef boost::circular_buffer<TimestampedLog> TimestampedLogBuffer;

	typedef boost::circular_buffer<string> SimpleLogBuffer;
	typedef StringKeyTable<SimpleLogBuffer> SimpleLogMap;
	struct AppGroupLog {
		TimestampedLogBuffer pidLog; // combined logs from PIDs
		SimpleLogMap watchFileLog; // a separate log buffer per (watched file name)
	};
	typedef StringKeyTable<AppGroupLog> LogStore;
	LogStore logStore;

public:
	Context(const Json::Value &initialConfig = Json::Value(),
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator());
	~Context();
	ConfigKit::Store getConfig() const;

	// specifically for logging output from application processes
	void saveNewLog(const HashedStaticString &groupName, const char *sourceStr, unsigned int sourceStrLen, const char *message, unsigned int messageLen);
	void saveMonitoredFileLog(const HashedStaticString &groupName,
		const char *sourceStr, unsigned int sourceStrLen,
		const char *content, unsigned int contentLen);
	// snapshot logStore to a JSON structure for external relay
	Json::Value convertLog();

	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors,
		LoggingKit::ConfigChangeRequest &req);
	void commitConfigChange(LoggingKit::ConfigChangeRequest &req)
		BOOST_NOEXCEPT_OR_NOTHROW;
	Json::Value inspectConfig() const;

	OXT_FORCE_INLINE
	const ConfigRealization *getConfigRealization() const {
		return configRlz.load(boost::memory_order_acquire);
	}

	void pushOldConfigAndCreateGcThread(ConfigRealization *oldConfigRlz, MonotonicTimeUsec monotonicNow);
	void gcThreadMain();

private:
	pair<ConfigRealization*,MonotonicTimeUsec> peekOldConfig();
	void popOldConfig(ConfigRealization *oldConfig);
	bool oldConfigsExist();
	void createGcThread();
	void killGcThread();
	void gcLockless(bool wait, boost::unique_lock<boost::mutex> &lock);
};


void initialize(const Json::Value &initialConfig = Json::Value(),
	const ConfigKit::Translator &translator = ConfigKit::DummyTranslator());


} // namespace LoggingKit
} // namespace Passenger

#endif /* _PASSENGER_LOGGING_KIT_CONTEXT_H_ */

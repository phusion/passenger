/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2015 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_DUMMY_SPAWNER_H_
#define _PASSENGER_SPAWNING_KIT_DUMMY_SPAWNER_H_

#include <Core/SpawningKit/Spawner.h>
#include <boost/atomic.hpp>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class DummySpawner: public Spawner {
private:
	boost::atomic<unsigned int> count;

public:
	unsigned int cleanCount;

	DummySpawner(const ConfigPtr &_config)
		: Spawner(_config),
		  count(1),
		  cleanCount(0)
		{ }

	virtual Result spawn(const Options &options) {
		TRACE_POINT();
		possiblyRaiseInternalError(options);

		syscalls::usleep(config->spawnTime);

		SocketPair adminSocket = createUnixSocketPair(__FILE__, __LINE__);
		unsigned int number = count.fetch_add(1, boost::memory_order_relaxed);
		Result result;
		Json::Value socket;

		socket["name"] = "main";
		socket["address"] = "tcp://127.0.0.1:1234";
		socket["protocol"] = "session";
		socket["concurrency"] = config->concurrency;

		result["type"] = "dummy";
		result["pid"] = number;
		result["gupid"] = "gupid-" + toString(number);
		result["spawner_creation_time"] = (Json::UInt64) SystemTime::getUsec();
		result["spawn_start_time"] = (Json::UInt64) SystemTime::getUsec();
		result["sockets"].append(socket);
		result.adminSocket = adminSocket.second;

		return result;
	}

	virtual bool cleanable() const {
		return true;
	}

	virtual void cleanup() {
		cleanCount++;
	}
};

typedef boost::shared_ptr<DummySpawner> DummySpawnerPtr;


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_DUMMY_SPAWNER_H_ */

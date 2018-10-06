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
#ifndef _PASSENGER_SPAWNING_KIT_DUMMY_SPAWNER_H_
#define _PASSENGER_SPAWNING_KIT_DUMMY_SPAWNER_H_

#include <boost/shared_ptr.hpp>
#include <boost/atomic.hpp>
#include <vector>

#include <StaticString.h>
#include <StrIntTools/StrIntUtils.h>
#include <Core/SpawningKit/Spawner.h>
#include <Core/SpawningKit/Exceptions.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class DummySpawner: public Spawner {
private:
	boost::atomic<unsigned int> count;

	void setConfigFromAppPoolOptions(Config *config, Json::Value &extraArgs,
		const AppPoolOptions &options)
	{
		Spawner::setConfigFromAppPoolOptions(config, extraArgs, options);
		config->spawnMethod = P_STATIC_STRING("dummy");
	}

public:
	unsigned int cleanCount;

	DummySpawner(Context *context)
		: Spawner(context),
		  count(1),
		  cleanCount(0)
		{ }

	virtual Result spawn(const AppPoolOptions &options) {
		TRACE_POINT();
		possiblyRaiseInternalError(options);

		if (context->debugSupport != NULL) {
			syscalls::usleep(context->debugSupport->dummySpawnDelay);
		}

		Config config;
		Json::Value extraArgs;
		setConfigFromAppPoolOptions(&config, extraArgs, options);

		unsigned int number = count.fetch_add(1, boost::memory_order_relaxed);
		Result result;
		Result::Socket socket;

		socket.address = "tcp://127.0.0.1:1234";
		socket.protocol = "session";
		socket.concurrency = 1;
		socket.acceptHttpRequests = true;
		if (context->debugSupport != NULL) {
			socket.concurrency = context->debugSupport->dummyConcurrency;
		}

		result.initialize(*context, &config);
		result.pid = number;
		result.type = Result::DUMMY;
		result.gupid = "gupid-" + toString(number);
		result.spawnEndTime = result.spawnStartTime;
		result.spawnEndTimeMonotonic = result.spawnStartTimeMonotonic;
		result.sockets.push_back(socket);

		vector<StaticString> internalFieldErrors;
		vector<StaticString> appSuppliedFieldErrors;
		if (!result.validate(internalFieldErrors, appSuppliedFieldErrors)) {
			Journey journey(SPAWN_DIRECTLY, !config.genericApp && config.startsUsingWrapper);
			journey.setStepErrored(SPAWNING_KIT_HANDSHAKE_PERFORM, true);
			SpawnException e(INTERNAL_ERROR, journey, &config);
			e.setSummary("Error spawning the web application:"
				" a bug in " SHORT_PROGRAM_NAME " caused the"
				" spawn result to be invalid: "
				+ toString(internalFieldErrors)
				+ ", " + toString(appSuppliedFieldErrors));
			e.setProblemDescriptionHTML(
				"Bug: the spawn result is invalid: "
				+ toString(internalFieldErrors)
				+ ", " + toString(appSuppliedFieldErrors));
			throw e.finalize();
		}

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

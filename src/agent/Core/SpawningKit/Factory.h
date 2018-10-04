/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SPAWNING_KIT_FACTORY_H_
#define _PASSENGER_SPAWNING_KIT_FACTORY_H_

#include <Core/SpawningKit/Context.h>
#include <Core/SpawningKit/SmartSpawner.h>
#include <Core/SpawningKit/DirectSpawner.h>
#include <Core/SpawningKit/DummySpawner.h>

namespace Passenger {
namespace SpawningKit {

using namespace std;
using namespace boost;
using namespace oxt;


class Factory {
private:
	boost::mutex syncher;
	Context *context;
	DummySpawnerPtr dummySpawner;

	SpawnerPtr tryCreateSmartSpawner(const AppPoolOptions &options) {
		string dir = context->resourceLocator->getHelperScriptsDir();
		vector<string> preloaderCommand;
		if (options.appType == "ruby" || options.appType == "rack") {
			preloaderCommand.push_back(options.ruby);
			preloaderCommand.push_back(dir + "/rack-preloader.rb");
		} else {
			return SpawnerPtr();
		}
		return boost::make_shared<SmartSpawner>(context,
			preloaderCommand, options);
	}

public:
	unsigned int spawnerCreationSleepTime;

	Factory(Context *_context)
		: context(_context),
		  spawnerCreationSleepTime(0)
	{
		if (context->debugSupport != NULL) {
			spawnerCreationSleepTime = context->debugSupport->spawnerCreationSleepTime;
		}
	}

	virtual ~Factory() { }

	virtual SpawnerPtr create(const AppPoolOptions &options) {
		if (options.spawnMethod == "smart" || options.spawnMethod == "smart-lv2") {
			SpawnerPtr spawner = tryCreateSmartSpawner(options);
			if (spawner == NULL) {
				spawner = boost::make_shared<DirectSpawner>(context);
			}
			return spawner;
		} else if (options.spawnMethod == "direct" || options.spawnMethod == "conservative") {
			boost::shared_ptr<DirectSpawner> spawner = boost::make_shared<DirectSpawner>(
				context);
			return spawner;
		} else if (options.spawnMethod == "dummy") {
			syscalls::usleep(spawnerCreationSleepTime);
			return getDummySpawner();
		} else {
			throw ArgumentException("Unknown spawn method '" + options.spawnMethod + "'");
		}
	}

	/**
	 * SpawnerFactory always returns the same DummyFactory object upon
	 * creating a dummy spawner. This allows unit tests to easily
	 * set debugging options on the spawner.
	 */
	DummySpawnerPtr getDummySpawner() {
		boost::lock_guard<boost::mutex> l(syncher);
		if (dummySpawner == NULL) {
			dummySpawner = boost::make_shared<DummySpawner>(context);
		}
		return dummySpawner;
	}

	/**
	 * All created Spawner objects share the same Context object.
	 */
	Context *getContext() const {
		return context;
	}
};

typedef boost::shared_ptr<Factory> FactoryPtr;


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_FACTORY_H_ */

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
#ifndef _PASSENGER_APPLICATION_POOL2_SPAWNER_FACTORY_H_
#define _PASSENGER_APPLICATION_POOL2_SPAWNER_FACTORY_H_

#include <ApplicationPool2/Spawner.h>
#include <ApplicationPool2/SmartSpawner.h>
#include <ApplicationPool2/DirectSpawner.h>
#include <ApplicationPool2/DummySpawner.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;


class SpawnerFactory {
private:
	SafeLibevPtr libev;
	ResourceLocator resourceLocator;
	ServerInstanceDir::GenerationPtr generation;
	RandomGeneratorPtr randomGenerator;
	boost::mutex syncher;
	SpawnerConfigPtr config;
	DummySpawnerPtr dummySpawner;
	
	SpawnerPtr tryCreateSmartSpawner(const Options &options) {
		string dir = resourceLocator.getHelperScriptsDir();
		vector<string> preloaderCommand;
		if (options.appType == "classic-rails") {
			preloaderCommand.push_back(options.ruby);
			preloaderCommand.push_back(dir + "/classic-rails-preloader.rb");
		} else if (options.appType == "rack") {
			preloaderCommand.push_back(options.ruby);
			preloaderCommand.push_back(dir + "/rack-preloader.rb");
		} else {
			return SpawnerPtr();
		}
		return boost::make_shared<SmartSpawner>(libev, resourceLocator,
			generation, preloaderCommand, options, config);
	}
	
public:
	SpawnerFactory(const SafeLibevPtr &_libev,
		const ResourceLocator &_resourceLocator,
		const ServerInstanceDir::GenerationPtr &_generation,
		const SpawnerConfigPtr &_config = SpawnerConfigPtr())
		: libev(_libev),
		  resourceLocator(_resourceLocator),
		  generation(_generation)
	{
		if (_config == NULL) {
			config = boost::make_shared<SpawnerConfig>();
		} else {
			config = _config;
		}
	}
	
	virtual ~SpawnerFactory() { }
	
	virtual SpawnerPtr create(const Options &options) {
		if (options.spawnMethod == "smart" || options.spawnMethod == "smart-lv2") {
			SpawnerPtr spawner = tryCreateSmartSpawner(options);
			if (spawner == NULL) {
				spawner = boost::make_shared<DirectSpawner>(libev,
					resourceLocator, generation, config);
			}
			return spawner;
		} else if (options.spawnMethod == "direct" || options.spawnMethod == "conservative") {
			boost::shared_ptr<DirectSpawner> spawner = boost::make_shared<DirectSpawner>(
				libev, resourceLocator, generation, config);
			return spawner;
		} else if (options.spawnMethod == "dummy") {
			syscalls::usleep(config->spawnerCreationSleepTime);
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
			dummySpawner = boost::make_shared<DummySpawner>(resourceLocator, config);
		}
		return dummySpawner;
	}

	/**
	 * All created Spawner objects share the same SpawnerConfig object.
	 */
	SpawnerConfigPtr getConfig() const {
		return config;
	}

	RandomGeneratorPtr getRandomGenerator() const {
		return randomGenerator;
	}

	const ResourceLocator &getResourceLocator() const {
		return resourceLocator;
	}
};

typedef boost::shared_ptr<SpawnerFactory> SpawnerFactoryPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_SPAWNER_FACTORY_H_ */

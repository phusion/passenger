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
#ifndef _PASSENGER_SPAWNING_KIT_CONFIG_H_
#define _PASSENGER_SPAWNING_KIT_CONFIG_H_

#include <boost/function.hpp>
#include <boost/make_shared.hpp>
#include <string>
#include <cstddef>

#include <ResourceLocator.h>
#include <RandomGenerator.h>
#include <Exceptions.h>
#include <Utils/VariantMap.h>
#include <Core/UnionStation/Context.h>

namespace Passenger {
namespace ApplicationPool2 {
	class Options;
} // namespace ApplicationPool2
} // namespace Passenger

namespace Passenger {
namespace SpawningKit {

using namespace std;


struct Config;
typedef ApplicationPool2::Options Options;
typedef boost::shared_ptr<Config> ConfigPtr;

typedef void (*ErrorHandler)(const ConfigPtr &config, SpawnException &e, const Options &options);
typedef boost::function<void (const char *data, unsigned int size)> OutputHandler;

struct Config {
	// Used by error pages and hooks.
	ResourceLocator *resourceLocator;
	const VariantMap *agentsOptions;
	ErrorHandler errorHandler;

	// Used for Union Station logging.
	UnionStation::ContextPtr unionStationContext;

	// Used by SmartSpawner and DirectSpawner.
	RandomGeneratorPtr randomGenerator;
	string instanceDir;

	// Used by DummySpawner and SpawnerFactory.
	unsigned int concurrency;
	unsigned int spawnerCreationSleepTime;
	unsigned int spawnTime;

	// Used by PipeWatcher.
	OutputHandler outputHandler;

	// Other.
	void *data;

	Config()
		: resourceLocator(NULL),
		  agentsOptions(NULL),
		  errorHandler(NULL),
		  concurrency(1),
		  spawnerCreationSleepTime(0),
		  spawnTime(0),
		  data(NULL)
		{ }

	void finalize() {
		TRACE_POINT();
		if (resourceLocator == NULL) {
			throw RuntimeException("ResourceLocator not initialized");
		}
		if (randomGenerator == NULL) {
			randomGenerator = boost::make_shared<RandomGenerator>();
		}
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_CONFIG_H_ */

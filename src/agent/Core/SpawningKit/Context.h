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
#ifndef _PASSENGER_SPAWNING_KIT_CONTEXT_H_
#define _PASSENGER_SPAWNING_KIT_CONTEXT_H_

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <string>
#include <algorithm>
#include <cstddef>

#include <ResourceLocator.h>
#include <RandomGenerator.h>
#include <Exceptions.h>
#include <WrapperRegistry/Registry.h>
#include <JsonTools/JsonUtils.h>
#include <ConfigKit/Store.h>

namespace Passenger {
	namespace ApplicationPool2 {
		class Options;
	}
}

namespace Passenger {
namespace SpawningKit {

using namespace std;


class HandshakePrepare;
typedef Passenger::ApplicationPool2::Options AppPoolOptions;


class Context {
public:
	class Schema: public ConfigKit::Schema {
	private:
		static void validate(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
			if (config["min_port_range"].asUInt() > config["max_port_range"].asUInt()) {
				errors.push_back(ConfigKit::Error(
					"'{{min_port_range}}' must be equal to or smaller than {{max_port_range}}"));
			}
			if (config["min_port_range"].asUInt() > 65535) {
				errors.push_back(ConfigKit::Error(
					"{{min_port_range}} must be equal to or less than 65535"));
			}
			if (config["max_port_range"].asUInt() > 65535) {
				errors.push_back(ConfigKit::Error(
					"{{max_port_range}} must be equal to or less than 65535"));
			}
		}

	public:
		Schema() {
			using namespace ConfigKit;

			add("min_port_range", UINT_TYPE, OPTIONAL, 5000);
			add("max_port_range", UINT_TYPE, OPTIONAL, 65535);

			addValidator(validate);
			finalize();
		}
	};

	struct DebugSupport {
		// Used by DummySpawner and SpawnerFactory.
		unsigned int dummyConcurrency;
		unsigned long long dummySpawnDelay;
		unsigned long long spawnerCreationSleepTime;

		DebugSupport()
			: dummyConcurrency(1),
			  dummySpawnDelay(0),
			  spawnerCreationSleepTime(0)
			{ }
	};

private:
	friend class HandshakePrepare;


	mutable boost::mutex syncher;


	/****** Context-global configuration ******/

	// Actual configuration store.
	ConfigKit::Store config;

	// Below follows cached values.

	// Other.
	unsigned int minPortRange, maxPortRange;


	/****** Working state ******/

	bool finalized;
	unsigned int nextPort;


	void updateConfigCache() {
		minPortRange = config["min_port_range"].asUInt();
		maxPortRange = config["max_port_range"].asUInt();
		nextPort = std::max(std::min(nextPort, maxPortRange), minPortRange);
	}

public:
	/****** Dependencies ******/

	const ResourceLocator *resourceLocator;
	const WrapperRegistry::Registry *wrapperRegistry;
	RandomGeneratorPtr randomGenerator;
	string integrationMode;
	string instanceDir;
	DebugSupport *debugSupport;
	//UnionStation::ContextPtr unionStationContext;


	Context(const Schema &schema, const Json::Value &initialConfig = Json::Value())
		: config(schema),

		  finalized(false),
		  nextPort(0),

		  resourceLocator(NULL),
		  wrapperRegistry(NULL),
		  debugSupport(NULL)
	{
		vector<ConfigKit::Error> errors;

        if (!config.update(initialConfig, errors)) {
            throw ArgumentException("Invalid initial configuration: "
                + toString(errors));
        }
        updateConfigCache();
	}

	Json::Value previewConfigUpdate(const Json::Value &updates,
		vector<ConfigKit::Error> &errors)
	{
		boost::lock_guard<boost::mutex> l(syncher);
		return config.previewUpdate(updates, errors);
	}

	bool configure(const Json::Value &updates, vector<ConfigKit::Error> &errors) {
		boost::lock_guard<boost::mutex> l(syncher);
		if (config.update(updates, errors)) {
			updateConfigCache();
			return true;
		} else {
			return false;
		}
	}

	Json::Value inspectConfig() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return config.inspect();
	}

	void finalize() {
		TRACE_POINT();
		if (resourceLocator == NULL) {
			throw RuntimeException("ResourceLocator not initialized");
		}
		if (wrapperRegistry == NULL) {
			throw RuntimeException("WrapperRegistry not initialized");
		}
		if (randomGenerator == NULL) {
			randomGenerator = boost::make_shared<RandomGenerator>();
		}
		if (integrationMode.empty()) {
			throw RuntimeException("integrationMode not set");
		}

		finalized = true;
	}

	bool isFinalized() const {
		return finalized;
	}
};

typedef boost::shared_ptr<Context> ContextPtr;


} // namespace SpawningKit
} // namespace Passenger

#endif /* _PASSENGER_SPAWNING_KIT_CONTEXT_H_ */

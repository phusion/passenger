/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_APPLICATION_POOL2_CONTEXT_H_
#define _PASSENGER_APPLICATION_POOL2_CONTEXT_H_

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/pool/object_pool.hpp>
#include <Exceptions.h>
#include <Core/SpawningKit/Factory.h>

namespace Passenger {
namespace ApplicationPool2 {


using namespace boost;

class Session;
class Process;


/**
 * State shared by Pool, Group, Process and Session. It contains statistics
 * and counters, memory management objects, configuration objects, etc.
 * This struct was introduced so that Group, Process and Sessions don't have
 * to depend on Pool (which introduces circular dependencies).
 *
 * The fields are separated in several groups. Each group may have its own mutex.
 * If it does, then all operations on any of the fields in that group requires
 * grabbing the mutex unless documented otherwise.
 */
struct Context {
public:
	/****** Working objects ******/

	boost::mutex memoryManagementSyncher;
	boost::object_pool<Session> sessionObjectPool;
	boost::object_pool<Process> processObjectPool;
	mutable boost::mutex agentConfigSyncher;


	/****** Dependencies ******/

	SpawningKit::FactoryPtr spawningKitFactory;
	Json::Value agentConfig;


	Context()
		: sessionObjectPool(64, 1024),
		  processObjectPool(4, 64)
		{ }

	void finalize() {
		if (spawningKitFactory == NULL) {
			throw RuntimeException("spawningKitFactory must be set");
		}
	}


	/****** Configuration objects ******/

	SpawningKit::Context *getSpawningKitContext() const {
		return spawningKitFactory->getContext();
	}

	const ResourceLocator *getResourceLocator() const {
		return getSpawningKitContext()->resourceLocator;
	}

	const WrapperRegistry::Registry *getWrapperRegistry() const {
		return getSpawningKitContext()->wrapperRegistry;
	}

	const RandomGeneratorPtr &getRandomGenerator() const {
		return getSpawningKitContext()->randomGenerator;
	}
};

typedef boost::shared_ptr<Context> ContextPtr;


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_CONTEXT_H_ */

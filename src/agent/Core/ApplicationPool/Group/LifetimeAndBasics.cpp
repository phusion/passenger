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
#include <Core/ApplicationPool/Group.h>

/*************************************************************************
 *
 * Functions for ApplicationPool2::Group for handling life time, basic info,
 * backreferences and related objects
 *
 *************************************************************************/

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;


/****************************
 *
 * Public methods
 *
 ****************************/


// Thread-safe.
bool
Group::isAlive() const {
	return getLifeStatus() == ALIVE;
}

// Thread-safe.
OXT_FORCE_INLINE
Group::LifeStatus
Group::getLifeStatus() const {
	return (LifeStatus) lifeStatus.load(boost::memory_order_seq_cst);
}

StaticString
Group::getName() const {
	return info.name;
}

const BasicGroupInfo &
Group::getInfo() {
	return info;
}

const ApiKey &
Group::getApiKey() const {
	return info.apiKey;
}

/**
 * Thread-safe.
 * @pre getLifeState() != SHUT_DOWN
 * @post result != NULL
 */
OXT_FORCE_INLINE Pool *
Group::getPool() const {
	return pool;
}

Context *
Group::getContext() const {
	return info.context;
}

psg_pool_t *
Group::getPallocPool() const {
	return getPool()->palloc;
}

const ResourceLocator &
Group::getResourceLocator() const {
	return *getPool()->getSpawningKitContext()->resourceLocator;
}

const WrapperRegistry::Registry &
Group::getWrapperRegistry() const {
	return *getPool()->getSpawningKitContext()->wrapperRegistry;
}


} // namespace ApplicationPool2
} // namespace Passenger

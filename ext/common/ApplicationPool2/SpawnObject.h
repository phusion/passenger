/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
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
#ifndef _PASSENGER_APPLICATION_POOL_SPAWNER_OBJECT_H_
#define _PASSENGER_APPLICATION_POOL_SPAWNER_OBJECT_H_

#include <boost/noncopyable.hpp>
#include <boost/move/core.hpp>
#include <MemoryKit/palloc.h>
#include <ApplicationPool2/Common.h>

namespace Passenger {
namespace ApplicationPool2 {


class SpawnObject: public boost::noncopyable {
private:
	BOOST_MOVABLE_BUT_NOT_COPYABLE(SpawnObject)

public:
	psg_pool_t *pool;
	ProcessPtr process;

	SpawnObject() {
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
	}

	SpawnObject(BOOST_RV_REF(SpawnObject) r)
		: pool(r.pool),
		  process(r.process)
	{
		r.pool = NULL;
		r.process.reset();
	}

	~SpawnObject() {
		if (pool != NULL) {
			psg_destroy_pool(pool);
		}
	}

	SpawnObject &operator=(BOOST_RV_REF(SpawnObject) r) {
		if (this != &r) {
			if (pool != r.pool) {
				if (pool != NULL) {
					psg_destroy_pool(pool);
				}
				pool = r.pool;
			}
			r.pool = NULL;
			process = r.process;
			r.process.reset();
		}
		return *this;
	}
};


} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL_SPAWNER_OBJECT_H_ */

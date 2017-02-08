/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2015-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_RELEASEABLE_SCOPED_POINTER_H_
#define _PASSENGER_RELEASEABLE_SCOPED_POINTER_H_

#include <boost/noncopyable.hpp>
#include <cstddef>

namespace Passenger {


using namespace std;

/**
 * This is like std::auto_ptr, but does not raise deprecation warnings on newer
 * compilers. We cannot replace std::auto_ptr with boost::scoped_ptr or
 * boost::shared_ptr because have a few use cases for std::auto_ptr.release().
 */
template<typename T>
class ReleaseableScopedPointer: public boost::noncopyable {
private:
	T *p;

public:
	ReleaseableScopedPointer(T *_p)
		: p(_p)
		{ }

	~ReleaseableScopedPointer() {
		delete p;
	}

	T *get() const {
		return p;
	}

	T *release() {
		T *tmp = p;
		p = NULL;
		return tmp;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_RELEASEABLE_SCOPED_POINTER_H_ */

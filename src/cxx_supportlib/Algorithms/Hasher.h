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
#ifndef _PASSENGER_ALGORITHMS_HASHER_H_
#define _PASSENGER_ALGORITHMS_HASHER_H_

#include <boost/cstdint.hpp>

namespace Passenger {


// TODO: use SpookyHash on x86_64
// TODO: use streaming murmurhash implementation: https://github.com/c9/murmur3

struct JenkinsHash {
	static const boost::uint32_t EMPTY_STRING_HASH = 0;

	boost::uint32_t hash;

	JenkinsHash()
		: hash(0)
		{ }

	void update(const char *data, unsigned int size);
	boost::uint32_t finalize();

	void reset() {
		hash = 0;
	}
};

typedef JenkinsHash Hasher;


} // namespace Passenger

#endif /* _PASSENGER_ALGORITHMS_HASHER_H_ */

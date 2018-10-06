/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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

#ifndef _PASSENGER_DATA_STRUCTURES_HASH_MAP_H_
#define _PASSENGER_DATA_STRUCTURES_HASH_MAP_H_

/*
 * There are too many ways to include hash_map/unordered_map!
 * This header autodetects the correct method.
 */
#if defined(HAS_UNORDERED_MAP)
	#include <unordered_map>
	#define HashMap std::unordered_map
#elif defined(HAS_TR1_UNORDERED_MAP)
	#include <tr1/unordered_map>
	#define HashMap std::tr1::unordered_map
#elif defined(HASH_NAMESPACE) && defined(HASH_MAP_HEADER)
	#ifndef _GLIBCXX_PERMIT_BACKWARD_HASH
		// Prevent deprecation warning on newer libstdc++ systems.
		// The warning suggests using unordered_map, but that is only
		// available when C++11 features are explicitly enabled.
		#define _GLIBCXX_PERMIT_BACKWARD_HASH
	#endif
	#include HASH_MAP_HEADER
	#define HashMap HASH_NAMESPACE::hash_map
#elif defined(__GNUC__)
	#include <ext/hash_map>
	#define HashMap __gnu_cxx::hash_map
#elif defined(_MSC_VER)
	#include <hash_map>
	#define HashMap stdext::hash_map
#else
	#include <boost/unordered_map.hpp>
	#define HashMap boost::unordered_map
#endif

#endif /* _PASSENGER_DATA_STRUCTURES_HASH_MAP_H_ */

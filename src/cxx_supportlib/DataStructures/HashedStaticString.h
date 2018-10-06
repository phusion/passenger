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
#ifndef _PASSENGER_DATA_STRUCTURES_HASHED_STATIC_STRING_H_
#define _PASSENGER_DATA_STRUCTURES_HASHED_STATIC_STRING_H_

#include <boost/cstdint.hpp>
#include <oxt/macros.hpp>
#include <string>
#include <StaticString.h>
#include <Algorithms/Hasher.h>

namespace Passenger {

using namespace std;


class HashedStaticString: public StaticString {
private:
	boost::uint32_t m_hash;

public:
	HashedStaticString()
		: StaticString(),
		  m_hash(Hasher::EMPTY_STRING_HASH)
		{ }

	HashedStaticString(const StaticString &b)
		: StaticString(b)
	{
		rehash();
	}

	HashedStaticString(const HashedStaticString &b)
		: StaticString(b),
		  m_hash(b.m_hash)
		{ }

	HashedStaticString(const string &s)
		: StaticString(s)
	{
		rehash();
	}

	HashedStaticString(const char *data)
		: StaticString(data)
	{
		rehash();
	}

	HashedStaticString(const char *data, string::size_type len)
		: StaticString(data, len)
	{
		rehash();
	}

	HashedStaticString(const char *data, string::size_type len,
		boost::uint32_t hash)
		: StaticString(data, len),
		  m_hash(hash)
		{ }

	void rehash() {
		Hasher h;
		h.update(data(), size());
		m_hash = h.finalize();
	}

	void setHash(boost::uint32_t value) {
		m_hash = value;
	}

	OXT_FORCE_INLINE
	boost::uint32_t hash() const {
		return m_hash;
	}
};


} // namespace Passenger

#endif /* _PASSENGER_DATA_STRUCTURES_HASHED_STATIC_STRING_H_ */

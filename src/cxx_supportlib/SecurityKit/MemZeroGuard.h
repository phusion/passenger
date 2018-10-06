/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_MEM_ZERO_GUARD_H_
#define _PASSENGER_MEM_ZERO_GUARD_H_

#include <string>

namespace Passenger {

using namespace std;


/**
 * Fills the given memory space or string with zeroes when a MemoryZeroGuard object
 * is destroyed. Useful for ensuring that buffers containing password data or
 * other sensitive information is cleared when it goes out of scope.
 */
class MemZeroGuard {
private:
	void *data;
	unsigned int size;
	string *str;

	static void securelyZeroMemory(volatile void *data, unsigned int size) {
		/* We do not use memset() here because the compiler may
		 * optimize out memset() calls. Instead, the following
		 * code is guaranteed to zero the memory.
		 * http://www.dwheeler.com/secure-programs/Secure-Programs-HOWTO/protect-secrets.html
		 */
		volatile char *p = (volatile char *) data;
		while (size--) {
			*p++ = 0;
		}
	}

public:
	/**
	 * Creates a new MemZeroGuard object with a memory region to zero.
	 *
	 * @param data The data to zero after destruction.
	 * @param size The size of the data.
	 * @pre data != NULL
	 */
	MemZeroGuard(void *data, unsigned int size) {
		this->data = data;
		this->size = size;
		this->str  = NULL;
	}

	/**
	 * Creates a new MemoryZeroGuard object with a string to zero.
	 *
	 * @param str The string to zero after destruction.
	 */
	MemZeroGuard(string &str) {
		this->data = NULL;
		this->size = 0;
		this->str  = &str;
	}

	/**
	 * Zero the data immediately. The data will still be zeroed after
	 * destruction of this object.
	 */
	void zeroNow() {
		if (str == NULL) {
			securelyZeroMemory(data, size);
		} else {
			securelyZeroMemory((volatile void *) str->c_str(), str->size());
		}
	}

	~MemZeroGuard() {
		zeroNow();
	}
};


} // namespace Passenger

#endif /* _PASSENGER_MEM_ZERO_GUARD_H_ */

/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_RANDOM_GENERATOR_H_
#define _PASSENGER_RANDOM_GENERATOR_H_

#include <string>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <oxt/system_calls.hpp>

#include "StaticString.h"
#include "Exceptions.h"


/**
 * A random 
 */
namespace Passenger {

using namespace std;
using namespace boost;
using namespace oxt;

/**
 * A random data generator. Data is generated using /dev/urandom, and
 * is cryptographically secure. Unlike rand() and friends,
 * RandomGenerator does not require seeding.
 *
 * The reason why RandomGenerator isn't a singleton is because opening
 * /dev/urandom is *very* slow on Mac OS X and OpenBSD. Each object of
 * this class caches the /dev/urandom file handle.
 *
 * This class is thread-safe as long as there are no concurrent
 * calls to reopen() or close().
 */
class RandomGenerator: public boost::noncopyable {
private:
	FILE *handle;
	
public:
	RandomGenerator(bool open = true) {
		handle = NULL;
		if (open) {
			reopen();
		}
	}
	
	~RandomGenerator() {
		this_thread::disable_syscall_interruption dsi;
		close();
	}
	
	void reopen() {
		close();
		handle = syscalls::fopen("/dev/urandom", "r");
		if (handle == NULL) {
			throw FileSystemException("Cannot open /dev/urandom",
				errno, "/dev/urandom");
		}
	}
	
	void close() {
		if (handle != NULL) {
			syscalls::fclose(handle);
			handle = NULL;
		}
	}
	
	StaticString generateBytes(void *buf, unsigned int size) {
		size_t ret = syscalls::fread(buf, 1, size, handle);
		if (ret != size) {
			throw IOException("Cannot read sufficient data from /dev/urandom");
		}
		return StaticString((const char *) buf, size);
	}
	
	string generateByteString(unsigned int size) {
		char buf[size];
		generateBytes(buf, size);
		return string(buf, size);
	}
	
	int generateInt() {
		int ret;
		generateBytes(&ret, sizeof(ret));
		return ret;
	}
	
	unsigned int generateUint() {
		return (unsigned int) generateInt();
	}
};

typedef shared_ptr<RandomGenerator> RandomGeneratorPtr;

} // namespace Passenger

#endif /* _PASSENGER_RANDOM_GENERATOR_H_ */

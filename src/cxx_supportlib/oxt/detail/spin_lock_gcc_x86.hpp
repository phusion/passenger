/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2010-2017 Phusion Holding B.V.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <boost/noncopyable.hpp>

namespace oxt {

/**
 * A spin lock. It's more efficient than a mutex for locking very small
 * critical sections with few contentions, but less efficient otherwise.
 *
 * The interface is similar to that of boost::mutex.
 */
class spin_lock {
private:
	volatile int exclusion;

public:
	/**
	 * Instantiate this class to lock a spin lock within a scope.
	 */
	class scoped_lock: boost::noncopyable {
	private:
		spin_lock &l;

	public:
		scoped_lock(spin_lock &lock): l(lock) {
			l.lock();
		}

		~scoped_lock() {
			l.unlock();
		}
	};

	spin_lock(): exclusion(0) { }

	/**
	 * Lock this spin lock.
	 * @throws boost::thread_resource_error Something went wrong.
	 */
	void lock() {
		while (__sync_lock_test_and_set(&exclusion, 1)) {
			// Do nothing. This GCC builtin instruction
			// ensures memory barrier.
		}
	}

	/**
	 * Unlock this spin lock.
	 * @throws boost::thread_resource_error Something went wrong.
	 */
	void unlock() {
		__sync_lock_release(&exclusion);
	}

	bool try_lock() {
		return !__sync_lock_test_and_set(&exclusion, 1);
	}
};

} // namespace oxt


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

#define OSSPINLOCK_DEPRECATED 0

#include <libkern/OSAtomic.h>
#include <boost/noncopyable.hpp>

/*
 * Implementation of a spin lock using Darwin spin locks.
 *
 * See spin_lock_gcc_x86.hpp for API documentation.
 */

namespace oxt {

class spin_lock {
private:
	OSSpinLock spin;

public:
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

	spin_lock() {
		spin = 0;
	}

	void lock() {
		OSSpinLockLock(&spin);
	}

	void unlock() {
		OSSpinLockUnlock(&spin);
	}

	bool try_lock() {
		return OSSpinLockTry(&spin);
	}
};

} // namespace oxt

/*
 * OXT - OS eXtensions for boosT
 * Provides important functionality necessary for writing robust server software.
 *
 * Copyright (c) 2010-2025 Asynchronous B.V.
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
#ifndef _OXT_SPIN_LOCK_HPP_
#define _OXT_SPIN_LOCK_HPP_

#include <thread>
#include <atomic>

namespace oxt {

/**
 * A spin lock. It's more efficient than a mutex for locking very small
 * critical sections with few contentions, but less efficient otherwise.
 *
 * The interface is similar to that of boost::mutex.
 */
class spin_lock {
private:
	std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
	/**
	* Instantiate this class to lock a spin lock within a scope.
	*/
	class scoped_lock {
	private:
		spin_lock &l;

	public:
		scoped_lock(const scoped_lock &other) = delete;
		scoped_lock &operator=(const scoped_lock &other) = delete;

		scoped_lock(spin_lock &lock) noexcept: l(lock) {
			l.lock();
		}

		~scoped_lock() noexcept {
			l.unlock();
		}
	};

	/**
	* Lock this spin lock.
	*/
	void lock() noexcept {
		while (true) {
			unsigned int i = 1000;
			while (i > 0) {
				if (!flag.test_and_set(std::memory_order_acquire)) {
					return;
				}
				i--;
			}

			// Yield the CPU every once in a while to allow other threads to
			// run. On systems with bad schedulers (including Valgrind),
			// not yielding can lead to starvation, which looks like a deadlock.
			std::this_thread::yield();
		}
	}

	/**
	* Unlock this spin lock.
	*/
	void unlock() noexcept {
		flag.clear(std::memory_order_release);
	}

	bool try_lock() noexcept {
		return !flag.test_and_set(std::memory_order_acquire);
	}
};

} // namespace oxt

#endif /* _OXT_SPIN_LOCK_HPP_ */

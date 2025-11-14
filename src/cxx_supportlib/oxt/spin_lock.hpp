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
 * A spin lock that's specifically designed to yield the CPU once in a while to
 * avoid starving other threads.
 *
 * We use spin locks because on many platforms, mutexes are slower when there's little
 * or no contention. OS-native mutexes work better in highly contended scenarios due
 * to better scheduler integration. However, in Passenger we have a couple of scenarios
 * where we know there's going to be little or no contention. In oxt::system_calls,
 * contention is rare and mostly limited to thread interruption during aborts or process
 * shutdown. As a result, we use spin locks to prioritize performance in the uncontended
 * case.
 *
 * We avoid OS-native spinlocks for two reasons:
 * - There's no widely-available standard spinlock. We want to keep the code simple,
 *   avoiding OS-specific implementations.
 * - OS-native spinlocks such as pthread_spin_lock_t don't guarantee scheduler integration,
 *   but we need it in order to avoid starvation that would interfere with thread
 *   interruption.
 */
class spin_lock {
private:
	// Our main use case is to allow threads to only be interruptable when
	// blocked on certain system calls (oxt::thread_local_context::syscall_interruption_lock).
	// That is locked most of the time, only unlocked during a syscall.
	// An oxt::thread::interrupt() call may need to wait for a long time before it's unlocked.
	// So keep the spin count low so we yield the CPU often.
	static constexpr unsigned int MAX_SPINS = 100;

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
			for (unsigned int i = 0; i < MAX_SPINS; i++) {
				if (flag.test_and_set(std::memory_order_acquire)) {
					#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
						flag.wait(true, std::memory_order_relaxed);
					#endif
				} else {
					return; // lock acquired
				}
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
		#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
			flag.notify_one();
		#endif
	}

	bool try_lock() noexcept {
		return !flag.test_and_set(std::memory_order_acquire);
	}
};

} // namespace oxt

#endif /* _OXT_SPIN_LOCK_HPP_ */

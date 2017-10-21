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
#ifndef _OXT_MACROS_HPP_
#define _OXT_MACROS_HPP_

/**
 * Specialized macros.
 *
 * These macros provide more specialized features which are not needed
 * so often by application programmers.
 */

#define OXT_GCC_VERSION (__GNUC__ * 10000 \
                           + __GNUC_MINOR__ * 100 \
                           + __GNUC_PATCHLEVEL__)

#if (defined(__GNUC__) && (__GNUC__ > 2)) || defined(IN_DOXYGEN)
	/**
	 * Indicate that the given expression is likely to be true.
	 * This allows the CPU to better perform branch prediction.
	 */
	#define OXT_LIKELY(expr) __builtin_expect((expr), 1)

	/**
	 * Indicate that the given expression is likely to be false.
	 * This allows the CPU to better perform branch prediction.
	 */
	#define OXT_UNLIKELY(expr) __builtin_expect((expr), 0)

	/**
	 * Force inlining of the given function.
	 */
	#define OXT_FORCE_INLINE __attribute__((always_inline))

	#define OXT_PURE __attribute__((pure))

	#if __GNUC__ >= 4
		#define OXT_RESTRICT __restrict__
	#else
		#define OXT_RESTRICT
	#endif
	#ifndef restrict
		/**
		 * The C99 'restrict' keyword, now usable in C++.
		 */
		#define restrict OXT_RESTRICT
	#endif
	#ifndef restrict_ref
		/**
		 * The C99 'restrict' keyword, for use with C++ references.
		 * On compilers that support 'restrict' in C++ but not on
		 * references, this macro does nothing.
		 */
		#define restrict_ref OXT_RESTRICT
	#endif
#else
	#define OXT_LIKELY(expr) expr
	#define OXT_UNLIKELY(expr) expr
	#define OXT_FORCE_INLINE
	#define OXT_PURE
	#define restrict
	#define restrict_ref
#endif

/*
 * GCC supports the __thread keyword on x86 since version 3.3, but versions earlier
 * than 4.1.2 have bugs (http://gcc.gnu.org/ml/gcc-bugs/2006-09/msg02275.html).
 *
 * FreeBSD 5 supports the __thread keyword, and everything works fine in
 * micro-tests, but in mod_passenger the thread-local variables are initialized
 * to unaligned addresses for some weird reason, thereby causing bus errors.
 *
 * GCC on OpenBSD supports __thread, but any access to such a variable
 * results in a segfault.
 *
 * Solaris does support __thread, but often it's not compiled into default GCC
 * packages (not to mention it's not available for Sparc). Playing it safe...
 *
 * On MacOS X, neither gcc nor llvm-gcc support the __thread keyword, but Clang
 * does. It works on at least clang >= 3.0.
 */
#ifndef PASSENGER_DISABLE_THREAD_LOCAL_STORAGE
	#if defined(__APPLE__)
		#if defined(__clang__) && __clang_major__ >= 3
			#define OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
		#endif
	#elif defined(__GNUC__) && OXT_GCC_VERSION >= 40102
		#if !defined(__SOLARIS__) && !defined(__OpenBSD__)
			#define OXT_THREAD_LOCAL_KEYWORD_SUPPORTED
		#endif
	#endif
#endif

#if defined(__has_feature)
	#if __has_feature(address_sanitizer)
		#define OXT_NO_SANITIZE(args) __attribute__((no_sanitize(args)))
	#endif
#endif
#ifndef OXT_NO_SANITIZE
	#define OXT_NO_SANITIZE(args)
#endif

#endif /* _OXT_MACROS_HPP_ */

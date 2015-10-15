/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010 Phusion Holding B.V.
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
#ifndef _PASSENGER_MEMORY_BARRIER_H_
#define _PASSENGER_MEMORY_BARRIER_H_

// Memory barrier macros. Also act as compiler barriers.

#if defined(__GNUC__) || defined(__INTEL_COMPILER)
	#if defined(__i386__)
		#if defined(HAS_SSE2) || (defined(HAS_LFENCE) && defined(HAS_SFENCE))
			#define P_READ_BARRIER() \
				do { __asm__ __volatile__ ("lfence" ::: "memory"); } while (false)
			#define P_WRITE_BARRIER() \
				do { __asm__ __volatile__ ("sfence" ::: "memory"); } while (false)
		#else
			#define P_READ_BARRIER() \
				do { __asm__ __volatile__ ("" ::: "memory"); } while (false)
			#define P_WRITE_BARRIER() \
				do { __asm__ __volatile__ ("lock; addl $0,0(%%esp)" ::: "memory"); } while (false)
		#endif

	#elif defined(__x86_64__)
		#define P_READ_BARRIER() \
			do { __asm__ __volatile__ ("lfence" ::: "memory"); } while (false)
		#define P_WRITE_BARRIER() \
			do { __asm__ __volatile__ ("sfence" ::: "memory"); } while (false)
	#endif
#endif

#endif /* _PASSENGER_MEMORY_BARRIER_H_ */

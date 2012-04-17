/*
 * Copyright (c) 2010 Phusion v.o.f.
 * https://github.com/FooBarWidget/boyer-moore-horspool
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
#ifndef _STREAM_BOYER_MOORE_HORSPOOL_
#define _STREAM_BOYER_MOORE_HORSPOOL_

/*
 * Boyer-Moore-Horspool string search algorithm implementation with streaming support.
 * Most string search algorithm implementations require the entire haystack data to
 * be in memory. In contrast, this implementation allows one to feed the haystack data
 * piece-of-piece in a "streaming" manner.
 *
 * This implementation is optimized for both speed and memory usage.
 * Other than the memory needed for the context structure, it does not perform any
 * additional memory allocations (except for minimal usage of the stack). The context
 * structure, which contains the Boyer-Moore-Horspool occurance table and various
 * state information, is is organized in such a way that it can be allocated with a
 * single memory allocation action, regardless of the length of the needle.
 * Its inner loop also deviates a little bit from the original algorithm: the original
 * algorithm matches data right-to-left, but this implementation first matches the
 * rightmost character, then matches the data left-to-right, thereby incorporating
 * some ideas from "Tuning the Boyer-Moore-Horspool String Searching Algorithm" by
 * Timo Raita, 1992. It uses memcmp() for this left-to-right match which is typically
 * heavily optimized.
 *
 * A few more notes:
 * - This code can be used for searching an arbitrary binary needle in an arbitrary binary
 *   haystack. It is not limited to text.
 * - Boyer-Moore-Horspool works best for long needles. Generally speaking, the longer the
 *   needle the faster the algorithm becomes. Thus, this implementation makes no effort
 *   at being fast at searching single-character needles or even short needles (say,
 *   less than 5 characters). You should just use memchr() and memmem() for that; those
 *   functions are usually heavily optimized (e.g. by using tricks like searching 4 bytes
 *   at the same time by treating data as an array of integers) and will probably be
 *   *much* faster than this code at searching short needles.
 * - You can further tweak this code to favor either memory usage or performance.
 *   See the typedef for sbmh_size_t for more information.
 *
 *
 * == Basic usage
 *
 * 1. Allocate a StreamBMH structure either on the stack (alloca) or on the heap.
 *    It must be at least SBMH_SIZE(needle_len) bytes big.
 *    The maximum supported needle size depends on the definition of sbmh_size_t. See
 *    its typedef for more information.
 *
 *    This structure contains haystack search state information and callback
 *    information. The section 'Reuse' explains why this is important.
 *
 * 2. Allocate a StreamBMH_Occ structure somewhere.
 *    This structure contains the Boyer-Moore-Horspool occurrance table. The section
 *    'Reuse' explains why this is important.
 *
 * 3. Initialize both structures with sbmh_init(). The structures are now usable for
 *    searching the given needle, and only the given needle.
 *    You must ensure that the StreamBMH structure has at least SBMH_SIZE(needle_len)
 *    bytes of space, otherwise sbmh_init() will overwrite too much memory.
 *    sbmh_init() does NOT make a copy of the needle data.
 *
 * 4. Feed haystack data using sbmh_feed(). You must pass it the same needle that you
 *    passed to sbmh_init(), and the same StreamBMH and StreamBMH_Occ structures.
 *    This library does not store a pointer to the needle passed to
 *    sbmh_init() for memory efficiency reasons: the caller already has a pointer
 *    to the needle data so there's no need for us to store it.
 *
 *    sbmh_feed() returns the number of bytes that has been analyzed:
 *
 *    - If the needle has now been found then the position of the last needle character
 *      in the currently fed data will be returned: all data until the end of the needle
 *      has been analyzed, but no more. Additionally, the 'found' field in the context
 *      structure will be set to true.
 *    - If the needle hasn't been found yet, then the size of the currently fed data
 *      will be returned: all fed data has been analyzed.
 *    - If the needle was already found, then any additional call to sbmh_feed()
 *      will cause it to return 0: nothing in the fed data is analyzed.
 *
 * There's no need deinitialize the StreamBMH/StreamBMH_Occ structures. Just free their
 * memory.
 *
 *
 * == Convenience
 *
 * There's a convenience macro, SBMH_ALLOC_AND_INIT(), for combining steps 1 and 2.
 * It accepts a NULL-terminated needle and allocates the StreamBMH structure using
 * malloc():
 *
 *   struct StreamBMH *ctx;
 *   SBMH_ALLOC_AND_INIT(ctx, "my needle");
 *   if (ctx == NULL) {
 *      // error...
 *   }
 *   ...
 *   free(ctx);
 *
 *
 * == Reusing: finding the same needle in a different haystack
 *
 * You can reuse the StreamBMH structure and the StreamBMH_Occ structure for
 * finding the same needle in a different haystack.
 *
 * StreamBMH contains the haystack search state. It must be reset every time
 * you want to search in a new haystack. Call sbmh_reset() to do so.
 *
 * The StreamBMH_Occ structure must not be changed because it only contains
 * needle-specific preparation data, not haystack-specific state. You can
 * just reuse the old StreamBMH_Occ structure.
 *
 * You can then call sbmh_feed() to analyze haystack data.
 *
 *
 * == Reusing: finding a different needle
 *
 * You can reuse an existing StreamBMH/StreamBMH_Occ structure for finding a
 * *different* needle as well. Call sbmh_init() to re-initialize both structures
 * for use with a different needle.
 * However you must make sure that the StreamBMH structure is at least
 * SBMH_SIZE(new_needle_len) bytes big.
 *
 *
 * == Multithreading
 *
 * Once initialized, it is safe to share a StreamBMH_Occ structure and the
 * needle among multiple threads as long as they don't modify either of these.
 * Each thread must however have its own StreamBMH structure.
 *
 *
 * == Recognition of non-needle data
 * 
 * The 'callback' field in the StreamBMH structure can be used for recognizing non-needle
 * data. This is especially useful for things like multipart MIME parsers where you're
 * interested in all data except for the needle.
 *
 * This callback is initially set to NULL by sbmh_init(). sbmh_reset() does not set it.
 * When set, sbmh_feed() will call this callback with any data that is determined to not
 * contain the needle. StreamBMH also has a 'user_data' field. You can set it to any
 * value for your own use; this code do not use it at all.
 *
 * The data passed to the callback can be either part of the data in sbmh_feed()'s
 * 'data' argument, or it can be part of the StreamBMH lookbehind buffer. If the latter
 * is the case, then consider the data only valid within the callback: once the
 * callback has finished, this code can do arbitrary things to the lookbehind buffer,
 * so to preserve that data you must make your own copy.
 */

/* This implementation is based on sample code originally written by Joel
 * Yliluoma <joel.yliluoma@iki.fi>, licensed under MIT.
 */

// We assume that other compilers support the 'restrict' keyword.
#ifdef __GNUC__
	#ifndef G_GNUC_RESTRICT
		#if defined (__GNUC__) && (__GNUC__ >= 4)
			#define G_GNUC_RESTRICT __restrict__
		#else
			#define G_GNUC_RESTRICT
		#endif
	#endif
	#ifndef restrict
		#define restrict G_GNUC_RESTRICT
	#endif
#endif

#ifndef likely
	#ifdef __GNUC__
		#define likely(expr) __builtin_expect((expr), 1)
		#define unlikely(expr) __builtin_expect((expr), 0)
	#else
		#define likely(expr) expr
		#define unlikely(expr) expr
	#endif
#endif


#include <cstddef>
#include <cstring>
#include <cassert>
#include <algorithm>


namespace Passenger {

struct StreamBMH;

/*
 * sbmh_size_t is a type for representing the needle length. It should be unsigned;
 * it makes no sense for it not to be.
 * By default it's typedef'ed to 'unsigned short', which is a 16-bit integer on most
 * platforms, allowing us to support needles up to about 64 KB. This ough to be enough
 * for most people. In the odd situation that you're dealing with extremely large
 * needles, you can typedef this to 'unsigned int' or even 'unsigned long long'.
 *
 * Its typedef slightly affects performance. Benchmarks on OS X Snow Leopard (x86_64)
 * have shown that typedeffing this to size_t (64-bit integer) makes the benchmark
 * 4-8% faster at the cost of 4 times more memory usage per StreamBMH structure.
 * Consider changing the typedef depending on your needs.
 */
typedef unsigned char sbmh_size_t;

typedef void (*sbmh_data_cb)(const struct StreamBMH *ctx, const unsigned char *data, size_t len);

struct StreamBMH_Occ {
	sbmh_size_t occ[256];
};

struct StreamBMH {
	/***** Public but read-only fields *****/
	bool          found;
	
	/***** Public fields; feel free to populate *****/
	sbmh_data_cb  callback;
	void         *user_data;
	
	/***** Internal fields, do not access. *****/
	sbmh_size_t   lookbehind_size;
	/* After this field comes a 'lookbehind' field whose size is determined
	 * by the allocator (e.g. SBMH_ALLOC_AND_INIT).
	 * Algorithm uses at most needle_len - 1 bytes of space in lookbehind buffer.
	 */
};

#define SBMH_SIZE(needle_len) (sizeof(struct StreamBMH) + (needle_len) - 1)
#define SBMH_ALLOC_AND_INIT(sbmh, needle) \
	do { \
		size_t needle_len = strlen((const char *) needle); \
		sbmh = (struct StreamBMH *) malloc(SBMH_SIZE(needle_len)); \
		sbmh_init(sbmh, (const unsigned char *) needle, needle_len); \
	} while (false)

#if 0
	#include <string>
	#include <cstdio>
	
	#define SBMH_DEBUG(format) printf(format)
	#define SBMH_DEBUG1(format, arg1) printf(format, arg1)
	#define SBMH_DEBUG2(format, arg1, arg2) printf(format, arg1, arg2)
#else
	#define SBMH_DEBUG(format) do { /* nothing */ } while (false)
	#define SBMH_DEBUG1(format, arg1) do { /* nothing */ } while (false)
	#define SBMH_DEBUG2(format, arg1, arg2) do { /* nothing */ } while (false)
#endif

/* Accessor for the lookbehind field. */
#define _SBMH_LOOKBEHIND(ctx) ((unsigned char *) ctx + sizeof(struct StreamBMH))


inline void
sbmh_reset(struct StreamBMH *restrict ctx) {
	ctx->found = false;
	ctx->lookbehind_size = 0;
}

inline void
sbmh_init(struct StreamBMH *restrict ctx, struct StreamBMH_Occ *restrict occ,
	const unsigned char *restrict needle, sbmh_size_t needle_len)
{
	sbmh_size_t i;
	unsigned int j;
	
	if (ctx != NULL) {
		sbmh_reset(ctx);
		ctx->callback = NULL;
		ctx->user_data = NULL;
	}
	
	if (occ != NULL) {
		assert(needle_len > 0);
		
		/* Initialize occurrance table. */
		for (j = 0; j < 256; j++) {
			occ->occ[j] = needle_len;
		}
		
		/* Populate occurance table with analysis of the needle,
		 * ignoring last letter.
		 */
		if (needle_len >= 1) {
			for (i = 0; i < needle_len - 1; i++) {
				occ->occ[needle[i]] = needle_len - 1 - i;
			}
		}
	}
}

inline char
sbmh_lookup_char(const struct StreamBMH *restrict ctx,
	const unsigned char *restrict data, ssize_t pos)
{
	if (pos < 0) {
		return _SBMH_LOOKBEHIND(ctx)[ctx->lookbehind_size + pos];
	} else {
		return data[pos];
	}
}

inline bool
sbmh_memcmp(const struct StreamBMH *restrict ctx,
	const unsigned char *restrict needle,
	const unsigned char *restrict data,
	ssize_t pos, sbmh_size_t len)
{
	ssize_t i = 0;
	
	while (i < ssize_t(len)) {
		unsigned char data_ch = sbmh_lookup_char(ctx, data, pos + i);
		unsigned char needle_ch = needle[i];
		
		if (data_ch == needle_ch) {
			i++;
		} else {
			return false;
		}
	}
	return true;
}

inline size_t
sbmh_feed(struct StreamBMH *restrict ctx, const struct StreamBMH_Occ *restrict occtable,
	const unsigned char *restrict needle, sbmh_size_t needle_len,
	const unsigned char *restrict data, size_t len)
{
	SBMH_DEBUG1("\n[sbmh] feeding: (%s)\n", std::string((const char *) data, len).c_str());
	
	if (ctx->found) {
		return 0;
	}
	
	/* Positive: points to a position in 'data'
	 *           pos == 3 points to data[3]
	 * Negative: points to a position in the lookbehind buffer
	 *           pos == -2 points to lookbehind[lookbehind_size - 2]
	 */
	ssize_t pos = -ctx->lookbehind_size;
	unsigned char last_needle_char = needle[needle_len - 1];
	const sbmh_size_t *occ = occtable->occ;
	unsigned char *lookbehind = _SBMH_LOOKBEHIND(ctx);
	
	if (pos < 0) {
		SBMH_DEBUG2("[sbmh] considering lookbehind: (%s)(%s)\n",
			std::string((const char *) lookbehind, ctx->lookbehind_size).c_str(),
			std::string((const char *) data, len).c_str());
		
		/* Lookbehind buffer is not empty. Perform Boyer-Moore-Horspool
		 * search with character lookup code that considers both the
		 * lookbehind buffer and the current round's haystack data.
		 *
		 * Loop until
		 *   there is a match.
		 * or until
		 *   we've moved past the position that requires the
		 *   lookbehind buffer. In this case we switch to the
		 *   optimized loop.
		 * or until
		 *   the character to look at lies outside the haystack.
		 */
		while (pos < 0 && pos <= ssize_t(len) - ssize_t(needle_len)) {
			 unsigned char ch = sbmh_lookup_char(ctx, data,
				pos + needle_len - 1);
			
			if (ch == last_needle_char
			 && sbmh_memcmp(ctx, needle, data, pos, needle_len - 1)) {
				ctx->found = true;
				ctx->lookbehind_size = 0;
				if (pos > -ctx->lookbehind_size && ctx->callback != NULL) {
					ctx->callback(ctx, lookbehind,
						ctx->lookbehind_size + pos);
				}
				SBMH_DEBUG1("[sbmh] found using lookbehind; end = %d\n",
					int(pos + needle_len));
				return pos + needle_len;
			} else {
				pos += occ[ch];
			}
		}
		
		// No match.
		
		if (pos < 0) {
			/* There's too few data for Boyer-Moore-Horspool to run,
			 * so let's use a different algorithm to skip as much as
			 * we can.
			 * Forward pos until
			 *   the trailing part of lookbehind + data
			 *   looks like the beginning of the needle
			 * or until
			 *   pos == 0
			 */
			SBMH_DEBUG1("[sbmh] inconclusive; pos = %d\n", (int) pos);
			while (pos < 0 && !sbmh_memcmp(ctx, needle, data, pos, len - pos)) {
				pos++;
			}
			SBMH_DEBUG1("[sbmh] managed to skip to pos = %d\n", (int) pos);
		}
		
		if (pos >= 0) {
			/* Discard lookbehind buffer. */
			SBMH_DEBUG("[sbmh] no match; discarding lookbehind\n");
			if (ctx->callback != NULL) {
				ctx->callback(ctx, lookbehind, ctx->lookbehind_size);
			}
			ctx->lookbehind_size = 0;
		} else {
			/* Cut off part of the lookbehind buffer that has
			 * been processed and append the entire haystack
			 * into it.
			 */
			sbmh_size_t bytesToCutOff = sbmh_size_t(ssize_t(ctx->lookbehind_size) + pos);
			
			if (bytesToCutOff > 0 && ctx->callback != NULL) {
				// The cut off data is guaranteed not to contain the needle.
				ctx->callback(ctx, lookbehind, bytesToCutOff);
			}
			
			memmove(lookbehind,
				lookbehind + bytesToCutOff,
				ctx->lookbehind_size - bytesToCutOff);
			ctx->lookbehind_size -= bytesToCutOff;
			
			assert(ssize_t(ctx->lookbehind_size + len) < ssize_t(needle_len));
			memcpy(lookbehind + ctx->lookbehind_size,
				data, len);
			ctx->lookbehind_size += len;
			
			SBMH_DEBUG1("[sbmh] update lookbehind -> (%s)\n",
				std::string((const char *) lookbehind, ctx->lookbehind_size).c_str());
			return len;
		}
	}
	
	assert(pos >= 0);
	assert(ctx->lookbehind_size == 0);
	
	SBMH_DEBUG1("[sbmh] starting from pos = %d\n", (int) pos);
	
	/* Lookbehind buffer is now empty. Perform Boyer-Moore-Horspool
	 * search with optimized character lookup code that only considers
	 * the current round's haystack data.
	 */
	while (likely( pos <= ssize_t(len) - ssize_t(needle_len) )) {
		unsigned char ch = data[pos + needle_len - 1];
		
		if (unlikely(
		        unlikely( ch == last_needle_char )
		     && unlikely( *(data + pos) == needle[0] )
		     && unlikely( memcmp(needle, data + pos, needle_len - 1) == 0 )
		)) {
			SBMH_DEBUG1("[sbmh] found at position %d\n", (int) pos);
			ctx->found = true;
			if (pos > 0 && ctx->callback != NULL) {
				ctx->callback(ctx, data, pos);
			}
			return pos + needle_len;
		} else {
			pos += occ[ch];
		}
	}
	
	/* There was no match. If there's trailing haystack data that we cannot
	 * match yet using the Boyer-Moore-Horspool algorithm (because the trailing
	 * data is less than the needle size) then match using a modified
	 * algorithm that starts matching from the beginning instead of the end.
	 * Whatever trailing data is left after running this algorithm is added to
	 * the lookbehind buffer.
	 */
	SBMH_DEBUG("[sbmh] no match\n");
	if (size_t(pos) < len) {
		while (size_t(pos) < len
		    && (
		          data[pos] != needle[0]
		       || memcmp(data + pos, needle, len - pos) != 0
		)) {
			pos++;
		}
		if (size_t(pos) < len) {
			memcpy(lookbehind, data + pos, len - pos);
			ctx->lookbehind_size = len - pos;
			SBMH_DEBUG2("[sbmh] adding %d trailing bytes to lookbehind -> (%s)\n",
				int(len - pos),
				std::string((const char *) lookbehind,
					ctx->lookbehind_size).c_str());
		}
	}
	
	/* Everything until pos is guaranteed not to contain needle data. */
	if (pos > 0 && ctx->callback != NULL) {
		ctx->callback(ctx, data, std::min(size_t(pos), len));
	}
	
	return len;
}

} // namespace Passenger

#endif /* _STREAM_BOYER_MOORE_HORSPOOL_ */

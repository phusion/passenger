/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
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
#ifndef _PASSENGER_HASHER_H_
#define _PASSENGER_HASHER_H_

#include <boost/cstdint.hpp>
#include <cstddef>

namespace Passenger {


// TODO: maybe use streaming murmurhash implementation: https://github.com/c9/murmur3

#ifdef __x86_64__
	class SpookyHash {
	public:
		//
		// Init: initialize the context of a SpookyHash
		// seed1: any 64-bit value will do, including 0
		// seed2: different seeds produce independent hashes
		SpookyHash()
			: m_length(0),
			  m_remainder(0)
		{
			m_state[0] = DEFAULT_SEED1;
			m_state[1] = DEFAULT_SEED2;
		}

		void reset() {
			m_length = 0;
			m_remainder = 0;
			m_state[0] = DEFAULT_SEED1;
			m_state[1] = DEFAULT_SEED2;
		}

		//
		// Update: add a piece of a message to a SpookyHash state
		//
		void update(
			const char *message,  // message fragment
			size_t length);       // length of message fragment in bytes


		//
		// Final: compute the hash for the current SpookyHash state
		//
		// This does not modify the state; you can keep updating it afterward
		//
		// The result is the same as if SpookyHash() had been called with
		// all the pieces concatenated into one message.
		//
		boost::uint32_t finalize();   // out only: second 64 bits of hash value.

	private:
		//
		// Short is used for messages under 192 bytes in length
		// Short has a low startup cost, the normal mode is good for long
		// keys, the cost crossover is at about 192 bytes.  The two modes were
		// held to the same quality bar.
		//
		static void Short(
			const void *message,  // message (array of bytes, not necessarily aligned)
			size_t length,        // length of message (in bytes)
			boost::uint64_t *hash1,        // in/out: in the seed, out the hash value
			boost::uint64_t *hash2);       // in/out: in the seed, out the hash value

		// number of boost::uint64_t's in internal state
		static const size_t sc_numVars = 12;

		// size of the internal state
		static const size_t sc_blockSize = sc_numVars*8;

		// size of buffer of unhashed data, in bytes
		static const size_t sc_bufSize = 2*sc_blockSize;

		//
		// sc_const: a constant which:
		//  * is not zero
		//  * is odd
		//  * is a not-very-regular mix of 1's and 0's
		//  * does not need any other special mathematical properties
		//
		static const boost::uint64_t sc_const = 0xdeadbeefdeadbeefLL;
		static const boost::uint64_t DEFAULT_SEED1 = 0;
		static const boost::uint64_t DEFAULT_SEED2 = 0;

		boost::uint64_t m_data[2*sc_numVars];   // unhashed data, for partial messages
		boost::uint64_t m_state[sc_numVars];  // internal state of the hash
		size_t m_length;             // total length of the input so far
		boost::uint8_t  m_remainder;          // length of unhashed data stashed in m_data
	};

	typedef SpookyHash Hasher;

#else
	struct JenkinsHash {
		boost::uint32_t hash;

		JenkinsHash()
			: hash(0)
			{ }

		void update(const char *data, unsigned int size);
		boost::uint32_t finalize();

		void reset() {
			hash = 0;
		}
	};

	typedef JenkinsHash Hasher;
#endif

} // namespace Passenger

#endif /* _PASSENGER_HASHER_H_ */

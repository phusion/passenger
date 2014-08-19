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

// Implementation is in its own file so that we can enable compiler optimizations for these functions only.

#include <Utils/Hasher.h>
#include <cstring>

namespace Passenger {

#ifdef __x86_64__

//
// left rotate a 64-bit value by k bytes
//
static inline boost::uint64_t Rot64(boost::uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

//
// This is used if the input is 96 bytes long or longer.
//
// The internal state is fully overwritten every 96 bytes.
// Every input bit appears to cause at least 128 bits of entropy
// before 96 other bytes are combined, when run forward or backward
//   For every input bit,
//   Two inputs differing in just that input bit
//   Where "differ" means xor or subtraction
//   And the base value is random
//   When run forward or backwards one Mix
// I tried 3 pairs of each; they all differed by at least 212 bits.
//
static inline void
Mix(
	const boost::uint64_t *data,
	boost::uint64_t &s0, boost::uint64_t &s1, boost::uint64_t &s2, boost::uint64_t &s3,
	boost::uint64_t &s4, boost::uint64_t &s5, boost::uint64_t &s6, boost::uint64_t &s7,
	boost::uint64_t &s8, boost::uint64_t &s9, boost::uint64_t &s10,boost::uint64_t &s11)
{
	s0 += data[0];    s2 ^= s10;    s11 ^= s0;    s0 = Rot64(s0,11);    s11 += s1;
	s1 += data[1];    s3 ^= s11;    s0 ^= s1;    s1 = Rot64(s1,32);    s0 += s2;
	s2 += data[2];    s4 ^= s0;    s1 ^= s2;    s2 = Rot64(s2,43);    s1 += s3;
	s3 += data[3];    s5 ^= s1;    s2 ^= s3;    s3 = Rot64(s3,31);    s2 += s4;
	s4 += data[4];    s6 ^= s2;    s3 ^= s4;    s4 = Rot64(s4,17);    s3 += s5;
	s5 += data[5];    s7 ^= s3;    s4 ^= s5;    s5 = Rot64(s5,28);    s4 += s6;
	s6 += data[6];    s8 ^= s4;    s5 ^= s6;    s6 = Rot64(s6,39);    s5 += s7;
	s7 += data[7];    s9 ^= s5;    s6 ^= s7;    s7 = Rot64(s7,57);    s6 += s8;
	s8 += data[8];    s10 ^= s6;    s7 ^= s8;    s8 = Rot64(s8,55);    s7 += s9;
	s9 += data[9];    s11 ^= s7;    s8 ^= s9;    s9 = Rot64(s9,54);    s8 += s10;
	s10 += data[10];    s0 ^= s8;    s9 ^= s10;    s10 = Rot64(s10,22);    s9 += s11;
	s11 += data[11];    s1 ^= s9;    s10 ^= s11;    s11 = Rot64(s11,46);    s10 += s0;
}

//
// Mix all 12 inputs together so that h0, h1 are a hash of them all.
//
// For two inputs differing in just the input bits
// Where "differ" means xor or subtraction
// And the base value is random, or a counting value starting at that bit
// The final result will have each bit of h0, h1 flip
// For every input bit,
// with probability 50 +- .3%
// For every pair of input bits,
// with probability 50 +- 3%
//
// This does not rely on the last Mix() call having already mixed some.
// Two iterations was almost good enough for a 64-bit result, but a
// 128-bit result is reported, so End() does three iterations.
//
static inline void
EndPartial(
	boost::uint64_t &h0, boost::uint64_t &h1, boost::uint64_t &h2, boost::uint64_t &h3,
	boost::uint64_t &h4, boost::uint64_t &h5, boost::uint64_t &h6, boost::uint64_t &h7,
	boost::uint64_t &h8, boost::uint64_t &h9, boost::uint64_t &h10,boost::uint64_t &h11)
{
	h11+= h1;    h2 ^= h11;   h1 = Rot64(h1,44);
	h0 += h2;    h3 ^= h0;    h2 = Rot64(h2,15);
	h1 += h3;    h4 ^= h1;    h3 = Rot64(h3,34);
	h2 += h4;    h5 ^= h2;    h4 = Rot64(h4,21);
	h3 += h5;    h6 ^= h3;    h5 = Rot64(h5,38);
	h4 += h6;    h7 ^= h4;    h6 = Rot64(h6,33);
	h5 += h7;    h8 ^= h5;    h7 = Rot64(h7,10);
	h6 += h8;    h9 ^= h6;    h8 = Rot64(h8,13);
	h7 += h9;    h10^= h7;    h9 = Rot64(h9,38);
	h8 += h10;   h11^= h8;    h10= Rot64(h10,53);
	h9 += h11;   h0 ^= h9;    h11= Rot64(h11,42);
	h10+= h0;    h1 ^= h10;   h0 = Rot64(h0,54);
}

static inline void
End(
	const boost::uint64_t *data,
	boost::uint64_t &h0, boost::uint64_t &h1, boost::uint64_t &h2, boost::uint64_t &h3,
	boost::uint64_t &h4, boost::uint64_t &h5, boost::uint64_t &h6, boost::uint64_t &h7,
	boost::uint64_t &h8, boost::uint64_t &h9, boost::uint64_t &h10,boost::uint64_t &h11)
{
	h0 += data[0];   h1 += data[1];   h2 += data[2];   h3 += data[3];
	h4 += data[4];   h5 += data[5];   h6 += data[6];   h7 += data[7];
	h8 += data[8];   h9 += data[9];   h10 += data[10]; h11 += data[11];
	EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
	EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
	EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
}

//
// The goal is for each bit of the input to expand into 128 bits of
//   apparent entropy before it is fully overwritten.
// n trials both set and cleared at least m bits of h0 h1 h2 h3
//   n: 2   m: 29
//   n: 3   m: 46
//   n: 4   m: 57
//   n: 5   m: 107
//   n: 6   m: 146
//   n: 7   m: 152
// when run forwards or backwards
// for all 1-bit and 2-bit diffs
// with diffs defined by either xor or subtraction
// with a base of all zeros plus a counter, or plus another bit, or random
//
static inline void
ShortMix(boost::uint64_t &h0, boost::uint64_t &h1, boost::uint64_t &h2, boost::uint64_t &h3)
{
	h2 = Rot64(h2,50);  h2 += h3;  h0 ^= h2;
	h3 = Rot64(h3,52);  h3 += h0;  h1 ^= h3;
	h0 = Rot64(h0,30);  h0 += h1;  h2 ^= h0;
	h1 = Rot64(h1,41);  h1 += h2;  h3 ^= h1;
	h2 = Rot64(h2,54);  h2 += h3;  h0 ^= h2;
	h3 = Rot64(h3,48);  h3 += h0;  h1 ^= h3;
	h0 = Rot64(h0,38);  h0 += h1;  h2 ^= h0;
	h1 = Rot64(h1,37);  h1 += h2;  h3 ^= h1;
	h2 = Rot64(h2,62);  h2 += h3;  h0 ^= h2;
	h3 = Rot64(h3,34);  h3 += h0;  h1 ^= h3;
	h0 = Rot64(h0,5);   h0 += h1;  h2 ^= h0;
	h1 = Rot64(h1,36);  h1 += h2;  h3 ^= h1;
}

//
// Mix all 4 inputs together so that h0, h1 are a hash of them all.
//
// For two inputs differing in just the input bits
// Where "differ" means xor or subtraction
// And the base value is random, or a counting value starting at that bit
// The final result will have each bit of h0, h1 flip
// For every input bit,
// with probability 50 +- .3% (it is probably better than that)
// For every pair of input bits,
// with probability 50 +- .75% (the worst case is approximately that)
//
static inline void
ShortEnd(boost::uint64_t &h0, boost::uint64_t &h1, boost::uint64_t &h2, boost::uint64_t &h3)
{
	h3 ^= h2;  h2 = Rot64(h2,15);  h3 += h2;
	h0 ^= h3;  h3 = Rot64(h3,52);  h0 += h3;
	h1 ^= h0;  h0 = Rot64(h0,26);  h1 += h0;
	h2 ^= h1;  h1 = Rot64(h1,51);  h2 += h1;
	h3 ^= h2;  h2 = Rot64(h2,28);  h3 += h2;
	h0 ^= h3;  h3 = Rot64(h3,9);   h0 += h3;
	h1 ^= h0;  h0 = Rot64(h0,47);  h1 += h0;
	h2 ^= h1;  h1 = Rot64(h1,54);  h2 += h1;
	h3 ^= h2;  h2 = Rot64(h2,32);  h3 += h2;
	h0 ^= h3;  h3 = Rot64(h3,25);  h0 += h3;
	h1 ^= h0;  h0 = Rot64(h0,63);  h1 += h0;
}

void SpookyHash::Short(
	const void *message,
	size_t length,
	boost::uint64_t *hash1,
	boost::uint64_t *hash2)
{
	union
	{
		const boost::uint8_t *p8;
		boost::uint32_t *p32;
		boost::uint64_t *p64;
		size_t i;
	} u;

	u.p8 = (const boost::uint8_t *)message;

	size_t remainder = length%32;
	boost::uint64_t a=*hash1;
	boost::uint64_t b=*hash2;
	boost::uint64_t c=sc_const;
	boost::uint64_t d=sc_const;

	if (length > 15)
	{
		const boost::uint64_t *end = u.p64 + (length/32)*4;

		// handle all complete sets of 32 bytes
		for (; u.p64 < end; u.p64 += 4)
		{
			c += u.p64[0];
			d += u.p64[1];
			ShortMix(a,b,c,d);
			a += u.p64[2];
			b += u.p64[3];
		}

		//Handle the case of 16+ remaining bytes.
		if (remainder >= 16)
		{
			c += u.p64[0];
			d += u.p64[1];
			ShortMix(a,b,c,d);
			u.p64 += 2;
			remainder -= 16;
		}
	}

	// Handle the last 0..15 bytes, and its length
	d += ((boost::uint64_t)length) << 56;
	switch (remainder)
	{
	case 15:
	d += ((boost::uint64_t)u.p8[14]) << 48;
	case 14:
		d += ((boost::uint64_t)u.p8[13]) << 40;
	case 13:
		d += ((boost::uint64_t)u.p8[12]) << 32;
	case 12:
		d += u.p32[2];
		c += u.p64[0];
		break;
	case 11:
		d += ((boost::uint64_t)u.p8[10]) << 16;
	case 10:
		d += ((boost::uint64_t)u.p8[9]) << 8;
	case 9:
		d += (boost::uint64_t)u.p8[8];
	case 8:
		c += u.p64[0];
		break;
	case 7:
		c += ((boost::uint64_t)u.p8[6]) << 48;
	case 6:
		c += ((boost::uint64_t)u.p8[5]) << 40;
	case 5:
		c += ((boost::uint64_t)u.p8[4]) << 32;
	case 4:
		c += u.p32[0];
		break;
	case 3:
		c += ((boost::uint64_t)u.p8[2]) << 16;
	case 2:
		c += ((boost::uint64_t)u.p8[1]) << 8;
	case 1:
		c += (boost::uint64_t)u.p8[0];
		break;
	case 0:
		c += sc_const;
		d += sc_const;
	}
	ShortEnd(a,b,c,d);
	*hash1 = a;
	*hash2 = b;
}

// add a message fragment to the state
void SpookyHash::update(const char *message, size_t length)
{
	boost::uint64_t h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11;
	size_t newLength = length + m_remainder;
	boost::uint8_t  remainder;
	union
	{
		const boost::uint8_t *p8;
		boost::uint64_t *p64;
		size_t i;
	} u;
	const boost::uint64_t *end;

	// Is this message fragment too short?  If it is, stuff it away.
	if (newLength < sc_bufSize)
	{
		memcpy(&((boost::uint8_t *)m_data)[m_remainder], message, length);
		m_length = length + m_length;
		m_remainder = (boost::uint8_t)newLength;
		return;
	}

	// init the variables
	if (m_length < sc_bufSize)
	{
		h0=h3=h6=h9  = m_state[0];
		h1=h4=h7=h10 = m_state[1];
		h2=h5=h8=h11 = sc_const;
	}
	else
	{
		h0 = m_state[0];
		h1 = m_state[1];
		h2 = m_state[2];
		h3 = m_state[3];
		h4 = m_state[4];
		h5 = m_state[5];
		h6 = m_state[6];
		h7 = m_state[7];
		h8 = m_state[8];
		h9 = m_state[9];
		h10 = m_state[10];
		h11 = m_state[11];
	}
	m_length = length + m_length;

	// if we've got anything stuffed away, use it now
	if (m_remainder)
	{
		boost::uint8_t prefix = sc_bufSize-m_remainder;
		memcpy(&(((boost::uint8_t *)m_data)[m_remainder]), message, prefix);
		u.p64 = m_data;
		Mix(u.p64, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
		Mix(&u.p64[sc_numVars], h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
		u.p8 = ((const boost::uint8_t *)message) + prefix;
		length -= prefix;
	}
	else
	{
		u.p8 = (const boost::uint8_t *)message;
	}

	// handle all whole blocks of sc_blockSize bytes
	end = u.p64 + (length/sc_blockSize)*sc_numVars;
	remainder = (boost::uint8_t)(length-((const boost::uint8_t *)end-u.p8));
	if ((u.i & 0x7) == 0)
	{
		while (u.p64 < end)
		{
			Mix(u.p64, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
		u.p64 += sc_numVars;
		}
	}
	else
	{
		while (u.p64 < end)
		{
			memcpy(m_data, u.p8, sc_blockSize);
			Mix(m_data, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
		u.p64 += sc_numVars;
		}
	}

	// stuff away the last few bytes
	m_remainder = remainder;
	memcpy(m_data, end, remainder);

	// stuff away the variables
	m_state[0] = h0;
	m_state[1] = h1;
	m_state[2] = h2;
	m_state[3] = h3;
	m_state[4] = h4;
	m_state[5] = h5;
	m_state[6] = h6;
	m_state[7] = h7;
	m_state[8] = h8;
	m_state[9] = h9;
	m_state[10] = h10;
	m_state[11] = h11;
}


// report the hash for the concatenation of all message fragments so far
boost::uint32_t SpookyHash::finalize()
{
	boost::uint64_t hash1, hash2;

	// init the variables
	if (m_length < sc_bufSize)
	{
		hash1 = m_state[0];
		hash2 = m_state[1];
		Short( m_data, m_length, &hash1, &hash2);
		return (boost::uint32_t) hash1;
	}

	const boost::uint64_t *data = (const boost::uint64_t *)m_data;
	boost::uint8_t remainder = m_remainder;

	boost::uint64_t h0 = m_state[0];
	boost::uint64_t h1 = m_state[1];
	boost::uint64_t h2 = m_state[2];
	boost::uint64_t h3 = m_state[3];
	boost::uint64_t h4 = m_state[4];
	boost::uint64_t h5 = m_state[5];
	boost::uint64_t h6 = m_state[6];
	boost::uint64_t h7 = m_state[7];
	boost::uint64_t h8 = m_state[8];
	boost::uint64_t h9 = m_state[9];
	boost::uint64_t h10 = m_state[10];
	boost::uint64_t h11 = m_state[11];

	if (remainder >= sc_blockSize)
	{
		// m_data can contain two blocks; handle any whole first block
		Mix(data, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
		data += sc_numVars;
		remainder -= sc_blockSize;
	}

	// mix in the last partial block, and the length mod sc_blockSize
	memset(&((boost::uint8_t *)data)[remainder], 0, (sc_blockSize-remainder));

	((boost::uint8_t *)data)[sc_blockSize-1] = remainder;

	// do some final mixing
	End(data, h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);

	return (boost::uint32_t) h0;
}


#else
	void
	JenkinsHash::update(const char *data, unsigned int size) {
		const char *end = data + size;

		while (data < end) {
			hash += *data;
			hash += (hash << 10);
			hash ^= (hash >> 6);
			data++;
		}
	}

	boost::uint32_t
	JenkinsHash::finalize() {
		hash += (hash << 3);
		hash ^= (hash >> 11);
		hash += (hash << 15);
		return hash;
	}
#endif

} // namespace Passenger

/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCH_H_
#define _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCH_H_

#include <boost/move/move.hpp>
#include <boost/container/string.hpp>
#include <boost/cstdint.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <cstddef>

#include <zlib.h>

#include <cstddef>
#include <cassert>

#include <DataStructures/StringKeyTable.h>
#include <MessageReadersWriters.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <UstRouter/Transaction.h>

namespace Passenger {
namespace UstRouter {
namespace RemoteSink {

using namespace std;
using namespace boost;


class Batch {
public:
	static const char MAGIC[];
	const unsigned int MAJOR_VERSION = 1;
	const unsigned int MINOR_VERSION = 0;

private:
	BOOST_MOVABLE_BUT_NOT_COPYABLE(Batch);

	static const unsigned int MAGIC_SIZE = 4;
	static const unsigned int PREAMBLE_SIZE_WITHOUT_HEADER = MAGIC_SIZE
		+ sizeof(boost::uint8_t)
		+ sizeof(boost::uint8_t)
		+ sizeof(boost::uint32_t);
	static const unsigned int ENTRY_HEADER_SIZE = sizeof(boost::uint32_t) * 2;


	size_t uncompressedSize;
	int compressionLevel;
	boost::container::string data;
	StringKeyTable<bool> keys;

	StaticString getPreambleHeader() const {
		return P_STATIC_STRING(
			"{\"client_software\": \"" PROGRAM_NAME "\","
			"\"client_software_version\": \"" PASSENGER_VERSION "\"}");
	}

	size_t createMetadataAndCalculateArchiveSize(Transaction *firstTxnInBatch) {
		size_t size = PREAMBLE_SIZE_WITHOUT_HEADER;
		Transaction *transaction = firstTxnInBatch;

		size += getPreambleHeader().size();

		do {
			transaction->createBatchArchiveMetadata();
			size += ENTRY_HEADER_SIZE
				+ transaction->getBatchArchiveMetadata().size()
				+ transaction->getBody().size();
			transaction = STAILQ_NEXT(transaction, nextInBatch);
		} while (transaction != NULL);

		return size;
	}

	void createArchiveData(z_stream &zlib, Transaction *firstTxnInBatch) {
		data.reserve(std::max<size_t>(uncompressedSize / 3, 1024));

		appendPreamble(zlib);

		Transaction *transaction = firstTxnInBatch;
		while (transaction != NULL) {
			appendEntry(zlib, transaction);
			registerKey(transaction->getUnionStationKey());
			transaction = STAILQ_NEXT(transaction, nextInBatch);
		}
	}

	void initCompression(z_stream &zlib) {
		if (compressionLevel != Z_NO_COMPRESSION) {
			zlib.zalloc = Z_NULL;
			zlib.zfree  = Z_NULL;
			zlib.opaque = Z_NULL;

			int ret = deflateInit(&zlib, compressionLevel);
			if (ret != Z_OK) {
				if (zlib.msg != NULL) {
					throw RuntimeException(P_STATIC_STRING("Cannot initialize zlib: ") +
						zlib.msg);
				} else {
					throw RuntimeException("Cannot initialize zlib");
				}
			}
		}
	}

	void finishCompression(z_stream &zlib) {
		if (compressionLevel != Z_NO_COMPRESSION) {
			appendBinary(zlib, StaticString(), Z_FINISH);
			deflateEnd(&zlib);
		}
	}

	void appendBinary(z_stream &zlib, const StaticString &data, int flush = Z_NO_FLUSH) {
		if (compressionLevel == Z_NO_COMPRESSION) {
			this->data.append(data.data(), data.size());
		} else {
			compressAndAppendBinary(zlib, data, flush);
		}
	}

	void compressAndAppendBinary(z_stream &zlib, const StaticString &data, int flush) {
		unsigned char out[256 * 1024];
		unsigned int have;
		bool done;
		int ret;

		zlib.avail_in = data.size();
		zlib.next_in = (unsigned char *) data.data();

		do {
			zlib.avail_out = sizeof(out);
			zlib.next_out  = out;
			ret = deflate(&zlib, flush);
			assert(ret != Z_STREAM_ERROR);

			have = sizeof(out) - zlib.avail_out;
			this->data.append((const char *) out, have);

			if (flush == Z_FINISH) {
				done = ret == Z_STREAM_END;
			} else {
				assert(ret != Z_STREAM_END);
				done = zlib.avail_out != 0;
			}
		} while (!done);

		assert(zlib.avail_in == 0);
	}

	void appendNumber8(z_stream &zlib, boost::uint8_t number) {
		appendBinary(zlib, StaticString((const char *) &number, 1));
	}

	void appendNumber32(z_stream &zlib, boost::uint32_t number) {
		char buf[sizeof(boost::uint32_t)];
		Uint32Message::generate(buf, number);
		appendBinary(zlib, StaticString(buf, sizeof(buf)));
	}

	void appendPreamble(z_stream &zlib) {
		appendBinary(zlib, StaticString(MAGIC, MAGIC_SIZE));
		appendNumber8(zlib, MAJOR_VERSION);
		appendNumber8(zlib, MINOR_VERSION);
		appendNumber32(zlib, getPreambleHeader().size());
		appendBinary(zlib, getPreambleHeader());
	}

	void appendEntry(z_stream &zlib, Transaction *transaction) {
		appendNumber32(zlib, transaction->getBatchArchiveMetadata().size());
		appendNumber32(zlib, transaction->getBody().size());
		appendBinary(zlib, transaction->getBatchArchiveMetadata());
		appendBinary(zlib, transaction->getBody());
	}

	void registerKey(const HashedStaticString &key) {
		keys.insert(key, true);
	}

public:
	Batch()
		: uncompressedSize(0),
		  compressionLevel(Z_DEFAULT_COMPRESSION)
		{ }

	Batch(Transaction *firstTxnInBatch, int _compressionLevel = Z_DEFAULT_COMPRESSION)
		: compressionLevel(_compressionLevel)
	{
		z_stream zlib;

		uncompressedSize = createMetadataAndCalculateArchiveSize(firstTxnInBatch);
		initCompression(zlib);
		createArchiveData(zlib, firstTxnInBatch);
		finishCompression(zlib);
	}

	Batch(BOOST_RV_REF(Batch) other)
		: uncompressedSize(other.uncompressedSize),
		  compressionLevel(other.compressionLevel),
		  data(boost::move(other.data)),
		  keys(boost::move(other.keys))
	{
		other.uncompressedSize = 0;
		other.compressionLevel = Z_DEFAULT_COMPRESSION;
	}

	Batch &operator=(BOOST_RV_REF(Batch) other) {
		if (this != &other) {
			uncompressedSize = other.uncompressedSize;
			compressionLevel = other.compressionLevel;
			data = boost::move(other.data);
			keys = boost::move(other.keys);
			other.uncompressedSize = 0;
			other.compressionLevel = Z_DEFAULT_COMPRESSION;
		}
		return *this;
	}

	StaticString getData() const {
		return StaticString(data.data(), data.size());
	}

	size_t getDataSize() const {
		return data.size();
	}

	bool isCompressed() const {
		return compressionLevel != Z_NO_COMPRESSION;
	}

	size_t getUncompressedSize() const {
		return uncompressedSize;
	}

	vector<string> getKeys() const {
		StringKeyTable<bool>::ConstIterator it(keys);
		vector<string> result;

		while (*it != NULL) {
			result.push_back(string(it.getKey().data(), it.getKey().size()));
			it.next();
		}

		return result;
	}
};


} // namespace RemoteSink
} // namespace UstRouter
} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_REMOTE_SINK_BATCH_H_ */

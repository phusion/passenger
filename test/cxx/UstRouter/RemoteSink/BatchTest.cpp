#include "TestSupport.h"
#include <zlib.h>
#include <MessageReadersWriters.h>
#include <UstRouter/RemoteSink/Batch.h>

using namespace Passenger;
using namespace Passenger::UstRouter;
using namespace Passenger::UstRouter::RemoteSink;
using namespace std;

namespace tut {
	struct UstRouter_RemoteSink_BatchTest {
		Transaction *txn, *txn2;
		StaticString metadata, metadata2;
		StaticString metadataBinarySize, metadataBinarySize2;
		StaticString bodyBinarySize, bodyBinarySize2;

		UstRouter_RemoteSink_BatchTest() {
			txn = new Transaction("txnId", "nodeName", "category",
				"unionStationKey", 1234, "filters");
			txn2 = new Transaction("txnId2", "nodeName2", "category2",
				"unionStationKey2", 4321, "filters2");
			STAILQ_NEXT(txn, nextInBatch) = txn2;

			txn->append("hello");
			txn2->append("my data");

			metadata = StaticString("{\"txn_id\": \"txnId\", "
				"\"node_name\": \"nodeName\", "
				"\"category\": \"category\", "
				"\"key\": \"unionStationKey\"}\n");
			metadata2 = StaticString("{\"txn_id\": \"txnId2\", "
				"\"node_name\": \"nodeName2\", "
				"\"category\": \"category2\", "
				"\"key\": \"unionStationKey2\"}\n");

			// Sizes of each transaction's batch archive metadata
			// in big-endian format
			metadataBinarySize = P_STATIC_STRING("\0\0\0\x5F");
			metadataBinarySize2 = P_STATIC_STRING("\0\0\0\x63");

			// Sizes of each transaction's body in big-endian format
			bodyBinarySize = P_STATIC_STRING("\0\0\0\x06");
			bodyBinarySize2 = P_STATIC_STRING("\0\0\0\x08");
		}

		~UstRouter_RemoteSink_BatchTest() {
			delete txn;
			delete txn2;
		}

		string decompress(const StaticString &data) {
			int ret;
			unsigned int have;
			z_stream strm;
			unsigned char out[1024];
			string result;

			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;
			strm.avail_in = 0;
			strm.next_in = Z_NULL;
			ret = inflateInit(&strm);
			if (ret != Z_OK) {
				fail("Cannot initialize zlib inflate");
			}

			strm.avail_in = data.size();
			strm.next_in = (Bytef *) data.data();

			do {
				strm.avail_out = sizeof(out);
				strm.next_out = out;

				ret = inflate(&strm, Z_SYNC_FLUSH);
				assert(ret != Z_STREAM_ERROR);
				switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;         /* and fall through */
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					inflateEnd(&strm);
					fail("Zlib decompression error");
				}

				have = sizeof(out) - strm.avail_out;
				result.append((const char *) out, have);
			} while (strm.avail_out == 0);

			inflateEnd(&strm);
			return result;
		}

		void testDecompressedData(const string &data) {
			const char *pos = data.data();
			const char *end = data.data() + data.size();

			StaticString expectedHeader = P_STATIC_STRING(
				"{\"client_software\": \"" PROGRAM_NAME "\","
				"\"client_software_version\": \"" PASSENGER_VERSION "\"}");

			////// Preamble

			// Magic
			ensure_equals("(Preamble 1)", StaticString(pos, 4), "USBF");
			pos += Batch::MAGIC_SIZE;

			// Major version
			ensure_equals("(Preamble 2)", StaticString(pos, sizeof(boost::uint8_t)),
				P_STATIC_STRING("\1"));
			pos += sizeof(boost::uint8_t);

			// Minor version
			ensure_equals("(Preamble 3)", StaticString(pos, sizeof(boost::uint8_t)),
				P_STATIC_STRING("\0"));
			pos += sizeof(boost::uint8_t);

			// Header size
			char expectedHeaderBinarySize[sizeof(boost::uint32_t)];
			Uint32Message::generate(expectedHeaderBinarySize, expectedHeader.size());
			ensure_equals("(Preamble 4)", StaticString(pos, sizeof(boost::uint32_t)),
				StaticString(expectedHeaderBinarySize, sizeof(expectedHeaderBinarySize)));
			pos += sizeof(boost::uint32_t);

			// Header
			ensure_equals("(Preamble 5)", StaticString(pos, expectedHeader.size()),
				expectedHeader);
			pos += expectedHeader.size();

			ensure("(Preamble: size check)", pos <= end);

			////// First entry

			// Header: metadata size
			ensure_equals("(Entry 1: metadata size)",
				StaticString(pos, sizeof(boost::uint32_t)),
				metadataBinarySize);
			pos += sizeof(boost::uint32_t);

			// Header: main payload size
			ensure_equals("(Entry 1: payload size)",
				StaticString(pos, sizeof(boost::uint32_t)),
				bodyBinarySize);
			pos += sizeof(boost::uint32_t);

			// Metadata
			ensure_equals("(Entry 1: metadata)",
				StaticString(pos, metadata.size()),
				metadata);
			pos += metadata.size();

			// Main payload
			ensure_equals("(Entry 1: payload)",
				StaticString(pos, txn->getBody().size()),
				txn->getBody());
			pos += txn->getBody().size();

			ensure("(Entry 1: size check)", pos <= end);

			////// Second entry

			// Header: metadata size
			ensure_equals("(Entry 2: metadata size)",
				StaticString(pos, sizeof(boost::uint32_t)),
				metadataBinarySize2);
			pos += sizeof(boost::uint32_t);

			// Header: main payload size
			ensure_equals("(Entry 2: payload size)",
				StaticString(pos, sizeof(boost::uint32_t)),
				bodyBinarySize2);
			pos += sizeof(boost::uint32_t);

			// Metadata
			ensure_equals("(Entry 2: metadata)",
				StaticString(pos, metadata2.size()),
				metadata2);
			pos += metadata2.size();

			// Main payload
			ensure_equals("(Entry 2: payload)",
				StaticString(pos, txn2->getBody().size()),
				txn2->getBody());
			pos += txn2->getBody().size();

			ensure_equals("(End of archive size check)", pos, end);
		}
	};

	DEFINE_TEST_GROUP(UstRouter_RemoteSink_BatchTest);

	TEST_METHOD(1) {
		set_test_name("Create with compression");
		Batch batch(txn);
		ensure(batch.isCompressed());

		string uncompressedData = decompress(batch.getData());
		ensure_equals(batch.getUncompressedSize(), uncompressedData.size());
		testDecompressedData(uncompressedData);
	}

	TEST_METHOD(2) {
		set_test_name("Create without compression");
		Batch batch(txn, Z_NO_COMPRESSION);
		ensure(!batch.isCompressed());
		ensure_equals(batch.getUncompressedSize(), batch.getData().size());
		testDecompressedData(batch.getData());
	}

	TEST_METHOD(3) {
		set_test_name("Move constructor with compression");
		Batch batch(txn);
		Batch batch2(boost::move(batch));

		ensure(batch.isCompressed());
		ensure_equals(batch.getUncompressedSize(), 0u);
		ensure_equals(batch.getData().size(), 0u);

		string decompressedData = decompress(batch2.getData());
		ensure(batch2.isCompressed());
		ensure_equals(batch2.getUncompressedSize(), decompressedData.size());
		testDecompressedData(decompressedData);
	}

	TEST_METHOD(4) {
		set_test_name("Move constructor without compression");
		Batch batch(txn, Z_NO_COMPRESSION);
		Batch batch2(boost::move(batch));

		ensure(batch.isCompressed());
		ensure_equals(batch.getUncompressedSize(), 0u);
		ensure_equals(batch.getData().size(), 0u);

		ensure(!batch2.isCompressed());
		ensure_equals(batch2.getUncompressedSize(), batch2.getData().size());
		testDecompressedData(batch2.getData());
	}

	TEST_METHOD(5) {
		set_test_name("Move operator with compression");
		Batch batch(txn);
		Batch batch2(txn2);
		batch2 = boost::move(batch);

		ensure(batch.isCompressed());
		ensure_equals(batch.getUncompressedSize(), 0u);
		ensure_equals(batch.getData().size(), 0u);

		string decompressedData = decompress(batch2.getData());
		ensure(batch2.isCompressed());
		ensure_equals(batch2.getUncompressedSize(), decompressedData.size());
		testDecompressedData(decompressedData);
	}

	TEST_METHOD(6) {
		set_test_name("Move constructor without compression");
		Batch batch(txn, Z_NO_COMPRESSION);
		Batch batch2(txn2);
		batch2 = boost::move(batch);

		ensure(batch.isCompressed());
		ensure_equals(batch.getUncompressedSize(), 0u);
		ensure_equals(batch.getData().size(), 0u);

		ensure(!batch2.isCompressed());
		ensure_equals(batch2.getUncompressedSize(), batch2.getData().size());
		testDecompressedData(batch2.getData());
	}
}

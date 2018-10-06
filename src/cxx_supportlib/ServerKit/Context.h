/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_CONTEXT_H_
#define _PASSENGER_SERVER_KIT_CONTEXT_H_

#include <string>
#include <boost/config.hpp>

#include <ServerKit/Config.h>
#include <ConfigKit/ConfigKit.h>
#include <MemoryKit/mbuf.h>
#include <LoggingKit/Assert.h>
#include <SafeLibev.h>
#include <Exceptions.h>
#include <Utils.h>
#include <JsonTools/JsonUtils.h>

extern "C" {
	struct uv_loop_s;
}

namespace Passenger {
namespace ServerKit {

using namespace std;


class Context {
private:
	ConfigKit::Store configStore;

public:
	typedef ServerKit::ConfigChangeRequest ConfigChangeRequest;

	// Dependencies
	SafeLibevPtr libev;
	struct uv_loop_s *libuv;

	// Others
	Config config;
	struct MemoryKit::mbuf_pool mbuf_pool;

	Context(const Schema &schema, const Json::Value &initialConfig = Json::Value(),
		const ConfigKit::Translator &translator = ConfigKit::DummyTranslator())
		: configStore(schema, initialConfig, translator),
		  libuv(NULL),
		  config(configStore)
		{ }

	~Context() {
		MemoryKit::mbuf_pool_deinit(&mbuf_pool);
	}

	void initialize() {
		if (libev == NULL) {
			throw RuntimeException("libev must be non-NULL");
		}
		if (libuv == NULL) {
			throw RuntimeException("libuv must be non-NULL");
		}

		mbuf_pool.mbuf_block_chunk_size = configStore["mbuf_block_chunk_size"].asUInt();
		MemoryKit::mbuf_pool_init(&mbuf_pool);
	}

	bool configure(const Json::Value &updates, vector<ConfigKit::Error> &errors) {
		ConfigChangeRequest req;
		bool result = prepareConfigChange(updates, errors, req);
		if (result) {
			commitConfigChange(req);
		}
		return result;
	}

	bool prepareConfigChange(const Json::Value &updates,
		vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
	{
		req.configStore.reset(new ConfigKit::Store(configStore, updates, errors));
		if (errors.empty()) {
			req.config.reset(new Config(*req.configStore));
		}
		return errors.empty();
	}

	void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
		configStore.swap(*req.configStore);
		config.swap(*req.config);
	}

	Json::Value inspectConfig() const {
		return configStore.inspect();
	}

	Json::Value inspectStateAsJson() const {
		Json::Value doc;
		Json::Value mbufDoc;

		mbufDoc["free_blocks"] = (Json::UInt) mbuf_pool.nfree_mbuf_blockq;
		mbufDoc["active_blocks"] = (Json::UInt) mbuf_pool.nactive_mbuf_blockq;
		mbufDoc["chunk_size"] = (Json::UInt) mbuf_pool.mbuf_block_chunk_size;
		mbufDoc["offset"] = (Json::UInt) mbuf_pool.mbuf_block_offset;
		mbufDoc["spare_memory"] = byteSizeToJson(mbuf_pool.nfree_mbuf_blockq
			* mbuf_pool.mbuf_block_chunk_size);
		mbufDoc["active_memory"] = byteSizeToJson(mbuf_pool.nactive_mbuf_blockq
			* mbuf_pool.mbuf_block_chunk_size);
		#ifdef MBUF_ENABLE_DEBUGGING
			struct MemoryKit::active_mbuf_block_list *list =
				const_cast<struct MemoryKit::active_mbuf_block_list *>(
					&mbuf_pool.active_mbuf_blockq);
			struct MemoryKit::mbuf_block *block;
			Json::Value listJson(Json::arrayValue);

			TAILQ_FOREACH (block, list, active_q) {
				Json::Value blockJson;
				blockJson["refcount"] = block->refcount;
				#ifdef MBUF_ENABLE_BACKTRACES
					blockJson["backtrace"] =
						(block->backtrace == NULL)
						? "(null)"
						: block->backtrace;
				#endif
				listJson.append(blockJson);
			}
			mbufDoc["active_blocks_list"] = listJson;
		#endif

		doc["mbuf_pool"] = mbufDoc;

		return doc;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CONTEXT_H_ */

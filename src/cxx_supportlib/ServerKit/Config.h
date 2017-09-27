/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_CONFIG_H_
#define _PASSENGER_SERVER_KIT_CONFIG_H_

// for std::swap()
#if __cplusplus >= 201103L
	#include <utility>
#else
	#include <algorithm>
#endif
#include <string>
#include <boost/config.hpp>
#include <boost/scoped_ptr.hpp>

#include <ConfigKit/ConfigKit.h>
#include <FileTools/PathManip.h>
#include <Constants.h>
#include <Utils.h>

namespace Passenger {
namespace ServerKit {

using namespace std;


/*
 * BEGIN ConfigKit schema: Passenger::ServerKit::Schema
 * (do not edit: following text is automatically generated
 * by 'rake configkit_schemas_inline_comments')
 *
 *   file_buffered_channel_auto_start_mover               boolean            -   default(true)
 *   file_buffered_channel_auto_truncate_file             boolean            -   default(true)
 *   file_buffered_channel_buffer_dir                     string             -   default
 *   file_buffered_channel_delay_in_file_mode_switching   unsigned integer   -   default(0)
 *   file_buffered_channel_max_disk_chunk_read_size       unsigned integer   -   default(0)
 *   file_buffered_channel_threshold                      unsigned integer   -   default(131072)
 *   mbuf_block_chunk_size                                unsigned integer   -   default(4096),read_only
 *   secure_mode_password                                 string             -   secret
 *
 * END
 */
class Schema: public ConfigKit::Schema {
private:
	static Json::Value getDefaultFileBufferedChannelBufferDir(const ConfigKit::Store &config) {
		return getSystemTempDir();
	}

	static Json::Value normalize(const Json::Value &effectiveValues) {
		Json::Value updates;

		updates["file_buffered_channel_buffer_dir"] = absolutizePath(
			effectiveValues["file_buffered_channel_buffer_dir"].asString());

		return updates;
	}

public:
	Schema() {
		using namespace ConfigKit;

		addWithDynamicDefault("file_buffered_channel_buffer_dir", STRING_TYPE,
			OPTIONAL | CACHE_DEFAULT_VALUE, getDefaultFileBufferedChannelBufferDir);
		add("file_buffered_channel_threshold", UINT_TYPE, OPTIONAL,
			DEFAULT_FILE_BUFFERED_CHANNEL_THRESHOLD);
		add("file_buffered_channel_delay_in_file_mode_switching", UINT_TYPE, OPTIONAL, 0);
		add("file_buffered_channel_max_disk_chunk_read_size", UINT_TYPE, OPTIONAL, 0);
		add("file_buffered_channel_auto_truncate_file", BOOL_TYPE, OPTIONAL, true);
		// For unit testing purposes
		add("file_buffered_channel_auto_start_mover", BOOL_TYPE, OPTIONAL, true);

		add("mbuf_block_chunk_size", UINT_TYPE, OPTIONAL | READ_ONLY,
			DEFAULT_MBUF_CHUNK_SIZE);
		add("secure_mode_password", STRING_TYPE, OPTIONAL | SECRET);

		addNormalizer(normalize);

		finalize();
	}
};

struct FileBufferedChannelConfig {
	string bufferDir;
	unsigned int threshold;
	unsigned int delayInFileModeSwitching;
	unsigned int maxDiskChunkReadSize;
	bool autoTruncateFile;
	bool autoStartMover;

	FileBufferedChannelConfig(const ConfigKit::Store &config)
		: bufferDir(config["file_buffered_channel_buffer_dir"].asString()),
		  threshold(config["file_buffered_channel_threshold"].asUInt()),
		  delayInFileModeSwitching(config["file_buffered_channel_delay_in_file_mode_switching"].asUInt()),
		  maxDiskChunkReadSize(config["file_buffered_channel_max_disk_chunk_read_size"].asUInt()),
		  autoTruncateFile(config["file_buffered_channel_auto_truncate_file"].asBool()),
		  autoStartMover(config["file_buffered_channel_auto_start_mover"].asBool())
		{ }

	void swap(FileBufferedChannelConfig &other) BOOST_NOEXCEPT_OR_NOTHROW {
		bufferDir.swap(other.bufferDir);
		std::swap(threshold, other.threshold);
		std::swap(delayInFileModeSwitching, other.delayInFileModeSwitching);
		std::swap(maxDiskChunkReadSize, other.maxDiskChunkReadSize);
		std::swap(autoTruncateFile, other.autoTruncateFile);
		std::swap(autoStartMover, other.autoStartMover);
	}
};

struct Config {
	string secureModePassword;
	FileBufferedChannelConfig fileBufferedChannelConfig;

	Config(const ConfigKit::Store &config)
		: secureModePassword(config["secure_mode_password"].asString()),
		  fileBufferedChannelConfig(config)
		{ }

	void swap(Config &other) BOOST_NOEXCEPT_OR_NOTHROW {
		secureModePassword.swap(other.secureModePassword);
		fileBufferedChannelConfig.swap(other.fileBufferedChannelConfig);
	}
};

struct ConfigChangeRequest {
	boost::scoped_ptr<ConfigKit::Store> configStore;
	boost::scoped_ptr<Config> config;
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_CONFIG_H_ */

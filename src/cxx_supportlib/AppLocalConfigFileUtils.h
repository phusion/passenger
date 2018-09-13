/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_APP_LOCAL_CONFIG_FILE_UTILS_H_
#define _PASSENGER_APP_LOCAL_CONFIG_FILE_UTILS_H_

#include <oxt/system_calls.hpp>
#include <oxt/backtrace.hpp>

#include <cerrno>
#include <fcntl.h>

#include <jsoncpp/json.h>

#include <StaticString.h>
#include <Constants.h>
#include <Exceptions.h>
#include <IOTools/IOUtils.h>
#include <Utils/ScopeGuard.h>

namespace Passenger {

using namespace std;


struct AppLocalConfig {
	string appStartCommand;
	bool appSupportsKuriaProtocol;

	AppLocalConfig()
		: appSupportsKuriaProtocol(false)
		{ }
};


inline AppLocalConfig
parseAppLocalConfigFile(const StaticString appRoot) {
	TRACE_POINT();
	string path = appRoot + "/Passengerfile.json";

	// Reading from Passengerfile.json from a root process is unsafe
	// because of symlink attacks and other kinds of attacks. See the
	// comments for safeReadFile().
	//
	// We are unable to use safeReadFile() here because we do not
	// control the safety of the directories leading up to appRoot.
	//
	// What we can do is preventing the contents of an arbitrary
	// file read from leaking out. Therefore, our result struct
	// only contains a limited number of fields, that are known
	// not to contain sensitive information. We also don't propagate
	// JSON parsing error messages, which may contain the content.

	int fd = syscalls::open(path.c_str(), O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		if (errno == ENOENT) {
			return AppLocalConfig();
		} else {
			int e = errno;
			throw FileSystemException("Error opening '" + path
				+ "' for reading", e, path);
		}
	}

	UPDATE_TRACE_POINT();
	FdGuard fdGuard(fd, __FILE__, __LINE__);
	pair<string, bool> content;
	try {
		content = readAll(fd, 1024 * 512);
	} catch (const SystemException &e) {
		throw FileSystemException("Error reading from '" + path + "'",
			e.code(), path);
	}
	if (!content.second) {
		throw SecurityException("Error parsing " + path
			+ ": file exceeds size limit of 512 KB");
	}
	fdGuard.runNow();

	UPDATE_TRACE_POINT();
	Json::Reader reader;
	Json::Value config;
	if (!reader.parse(content.first, config)) {
		if (geteuid() == 0) {
			throw RuntimeException("Error parsing " + path
				+ " (error messages suppressed for security reasons)");
		} else {
			throw RuntimeException("Error parsing " + path + ": "
				+ reader.getFormattedErrorMessages());
		}
	}
	// We no longer need the raw data so free the memory.
	content.first.resize(0);


	UPDATE_TRACE_POINT();
	AppLocalConfig result;

	if (!config.isObject()) {
		throw RuntimeException("Config file " + path
			+ " is not valid: top-level JSON object expected");
	}
	if (config.isMember("app_start_command")) {
		if (config["app_start_command"].isString()) {
			result.appStartCommand = config["app_start_command"].asString();
		} else {
			throw RuntimeException("Config file " + path
				+ " is not valid: key 'app_start_command' must be a boolean");
		}
	}
	if (config.isMember("app_supports_kuria_protocol")) {
		if (config["app_supports_kuria_protocol"].isBool()) {
			result.appSupportsKuriaProtocol = config["app_supports_kuria_protocol"].asBool();
		} else {
			throw RuntimeException("Config file " + path
				+ " is not valid: key 'app_supports_kuria_protocol' must be a boolean");
		}
	}

	return result;
}


} // namespace Passenger

#endif /* _PASSENGER_APP_LOCAL_CONFIG_FILE_UTILS_H_ */

/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011-2013 Phusion
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
#ifndef _PASSENGER_HELPER_AGENT_OPTIONS_H_
#define _PASSENGER_HELPER_AGENT_OPTIONS_H_

#include <sys/types.h>
#include <string>
#include <boost/shared_ptr.hpp>
#include <Utils/VariantMap.h>

namespace Passenger {

using namespace std;
using namespace boost;


struct AgentOptions: public VariantMap {
	pid_t   webServerPid;
	string  serverInstanceDir;
	string  tempDir;
	bool    userSwitching;
	string  defaultUser;
	string  defaultGroup;
	string  passengerRoot;
	string  defaultRubyCommand;
	unsigned int generationNumber;
	unsigned int maxPoolSize;
	unsigned int poolIdleTime;
	string requestSocketFilename;
	string requestSocketPassword;
	string adminSocketAddress;
	string exitPassword;
	string loggingAgentAddress;
	string loggingAgentPassword;
	string adminToolStatusPassword;
	vector<string> prestartUrls;

	bool testBinary;
	string requestSocketLink;

	AgentOptions() { }

	AgentOptions(const VariantMap &options)
		: VariantMap(options)
	{
		testBinary = options.get("test_binary", false) == "1";
		if (testBinary) {
			return;
		}

		// Required options for which a default is already set by the Watchdog.
		passengerRoot      = options.get("passenger_root");
		tempDir            = options.get("temp_dir");
		userSwitching      = options.getBool("user_switching");
		defaultRubyCommand = options.get("default_ruby");
		defaultUser        = options.get("default_user");
		defaultGroup       = options.get("default_group");
		maxPoolSize        = options.getInt("max_pool_size");
		poolIdleTime       = options.getInt("pool_idle_time");

		// Required options only set by the Watchdog.
		webServerPid          = options.getPid("web_server_pid");
		serverInstanceDir     = options.get("server_instance_dir");
		generationNumber      = options.getInt("generation_number");
		requestSocketFilename = options.get("request_socket_filename");
		requestSocketPassword = options.get("request_socket_password");
		if (requestSocketPassword == "-") {
			requestSocketPassword = "";
		}
		adminSocketAddress    = options.get("helper_agent_admin_socket_address");
		exitPassword          = options.get("helper_agent_exit_password");
		loggingAgentAddress   = options.get("logging_agent_address");
		loggingAgentPassword  = options.get("logging_agent_password");
		adminToolStatusPassword = options.get("admin_tool_status_password");

		// Optional options.
		prestartUrls          = options.getStrSet("prestart_urls", false);
		requestSocketLink     = options.get("request_socket_link", false);
	}
};

typedef boost::shared_ptr<AgentOptions> AgentOptionsPtr;


} // namespace Passenger

#endif /* _PASSENGER_HELPER_AGENT_OPTIONS_H_ */

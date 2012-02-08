#ifndef _PASSENGER_HELPER_AGENT_OPTIONS_H_
#define _PASSENGER_HELPER_AGENT_OPTIONS_H_

#include <sys/types.h>
#include <string>
#include <Utils/VariantMap.h>
#include <Utils/Base64.h>

namespace Passenger {

using namespace std;


struct AgentOptions {
	pid_t   webServerPid;
	string  tempDir;
	bool    userSwitching;
	string  defaultUser;
	string  defaultGroup;
	string  passengerRoot;
	string  rubyCommand;
	unsigned int generationNumber;
	unsigned int maxPoolSize;
	unsigned int maxInstancesPerApp;
	unsigned int poolIdleTime;
	string requestSocketPassword;
	string messageSocketPassword;
	string loggingAgentAddress;
	string loggingAgentPassword;
	string prestartUrls;

	AgentOptions() { }

	AgentOptions(const VariantMap &options) {
		webServerPid  = options.getPid("web_server_pid");
		tempDir       = options.get("temp_dir");
		userSwitching = options.getBool("user_switching");
		defaultUser   = options.get("default_user");
		defaultGroup  = options.get("default_group");
		passengerRoot = options.get("passenger_root");
		rubyCommand   = options.get("ruby");
		generationNumber   = options.getInt("generation_number");
		maxPoolSize        = options.getInt("max_pool_size");
		maxInstancesPerApp = options.getInt("max_instances_per_app");
		poolIdleTime       = options.getInt("pool_idle_time");
		requestSocketPassword = Base64::decode(options.get("request_socket_password"));
		messageSocketPassword = Base64::decode(options.get("message_socket_password"));
		loggingAgentAddress   = options.get("logging_agent_address");
		loggingAgentPassword  = options.get("logging_agent_password");
		prestartUrls          = options.get("prestart_urls");
	}
};


} // namespace Passenger

#endif /* _PASSENGER_HELPER_AGENT_OPTIONS_H_ */

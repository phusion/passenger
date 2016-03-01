/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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
#ifndef _PASSENGER_UST_ROUTER_OPTION_PARSER_H_
#define _PASSENGER_UST_ROUTER_OPTION_PARSER_H_

#include <cstdio>
#include <cstdlib>
#include <Constants.h>
#include <Utils.h>
#include <Utils/VariantMap.h>
#include <Utils/OptionParsing.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

using namespace std;


inline void
ustRouterUsage() {
	printf("Usage: " AGENT_EXE " ust-router <OPTIONS...>\n");
	printf("Runs the " PROGRAM_NAME " UstRouter.\n");
	printf("\n");
	printf("Required options:\n");
	printf("      --passenger-root PATH   The location to the " PROGRAM_NAME " source\n");
	printf("                              directory\n");
	printf("      --password-file PATH    Protect the UstRouter controller with the password in\n");
	printf("                              this file\n");
	printf("\n");
	printf("Socket options (optional):\n");
	printf("  -l, --listen ADDRESS        Listen on the given address. The address must be\n");
	printf("                              formatted as tcp://IP:PORT for TCP sockets, or\n");
	printf("                              unix:PATH for Unix domain sockets.\n");
	printf("                              " DEFAULT_UST_ROUTER_LISTEN_ADDRESS "\n");
	printf("\n");
	printf("      --api-listen ADDRESS    Listen on the given address for API commands.\n");
	printf("                              The address must be in the same format as that\n");
	printf("                              of --listen\n");
	printf("      --authorize [LEVEL]:USERNAME:PASSWORDFILE\n");
	printf("                              Enables authentication on the API server,\n");
	printf("                              through the given API account. LEVEL indicates\n");
	printf("                              the privilege level (see below). PASSWORDFILE must\n");
	printf("                              point to a file containing the password\n");
	printf("\n");
	printf("Operational options (optional):\n");
	printf("      --dev-mode              Enable development mode: dump data to a directory\n");
	printf("                              instead of sending them to the Union Station gateway\n");
	printf("      --dump-dir  PATH        Directory to dump to\n");
	printf("\n");
	printf("Other options (optional):\n");
	printf("      --user USERNAME         Lower privilege to the given user. Only has\n");
	printf("                              effect when started as root\n");
	printf("      --group GROUPNAME       Lower privilege to the given group. Only has\n");
	printf("                              effect when started as root. Default: primary\n");
	printf("                              group of the username given by '--user'\n");
	printf("\n");
	printf("      --log-file PATH         Log to the given file.\n");
	printf("      --log-level LEVEL       Logging level. Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("\n");
	printf("      --core-file-descriptor-ulimit NUMBER\n");
	printf("                              Set custom file descriptor ulimit for the core\n");
	printf("\n");
	printf("  -h, --help                  Show this help\n");
	printf("\n");
	printf("API account privilege levels (ordered from most to least privileges):\n");
	printf("  readonly    Read-only access\n");
	printf("  full        Full access (default)\n");
}

inline bool
parseUstRouterOption(int argc, const char *argv[], int &i, VariantMap &options) {
	OptionParser p(ustRouterUsage);

	if (p.isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
		options.set("passenger_root", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--password-file")) {
		options.set("ust_router_password_file", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], 'l', "--listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			options.set("ust_router_address", argv[i + 1]);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--api-listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			vector<string> addresses = options.getStrSet("ust_router_api_addresses",
				false);
			if (addresses.size() == SERVER_KIT_MAX_SERVER_ENDPOINTS) {
				fprintf(stderr, "ERROR: you may specify up to %u --api-listen addresses.\n",
					SERVER_KIT_MAX_SERVER_ENDPOINTS);
				exit(1);
			}
			addresses.push_back(argv[i + 1]);
			options.setStrSet("ust_router_api_addresses", addresses);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --api-listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--authorize")) {
		vector<string> args;
		vector<string> authorizations = options.getStrSet("ust_router_authorizations",
				false);

		split(argv[i + 1], ':', args);
		if (args.size() < 2 || args.size() > 3) {
			fprintf(stderr, "ERROR: invalid format for --authorize. The syntax "
				"is \"[LEVEL:]USERNAME:PASSWORDFILE\".\n");
			exit(1);
		}

		authorizations.push_back(argv[i + 1]);
		options.setStrSet("ust_router_authorizations", authorizations);
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--dev-mode")) {
		options.setBool("ust_router_dev_mode", true);
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--dump-dir")) {
		options.set("ust_router_dump_dir", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--user")) {
		options.set("analytics_log_user", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--group")) {
		options.set("analytics_log_group", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-level")) {
		// We do not set log_level because, when this function is called from
		// the Watchdog, we don't want to affect the Watchdog's own log level.
		options.setInt("ust_router_log_level", atoi(argv[i + 1]));
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-file")) {
		// We do not set debug_log_file because, when this function is called from
		// the Watchdog, we don't want to affect the Watchdog's own log file.
		options.set("ust_router_log_file", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--core-file-descriptor-ulimit")) {
		options.setUint("core_file_descriptor_ulimit", atoi(argv[i + 1]));
		i += 2;
	} else {
		return false;
	}
	return true;
}


} // namespace Passenger

#endif /* _PASSENGER_UST_ROUTER_OPTION_PARSER_H_ */

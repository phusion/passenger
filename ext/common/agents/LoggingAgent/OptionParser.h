/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2014 Phusion
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
#ifndef _PASSENGER_LOGGING_AGENT_OPTION_PARSER_H_
#define _PASSENGER_LOGGING_AGENT_OPTION_PARSER_H_

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
loggingAgentUsage() {
	printf("Usage: " AGENT_EXE " logger <OPTIONS...>\n");
	printf("Runs the " PROGRAM_NAME " logging agent.\n");
	printf("\n");
	printf("Required options:\n");
	printf("      --passenger-root PATH   The location to the " PROGRAM_NAME " source\n");
	printf("                              directory\n");
	printf("      --password-file PATH    Protect the logging server with the password in\n");
	printf("                              this file\n");
	printf("\n");
	printf("Other options (optional):\n");
	printf("  -l, --listen ADDRESS        Listen on the given address. The address must be\n");
	printf("                              formatted as tcp://IP:PORT for TCP sockets, or\n");
	printf("                              unix:PATH for Unix domain sockets.\n");
	printf("                              " DEFAULT_LOGGING_AGENT_LISTEN_ADDRESS "\n");
	printf("\n");
	printf("      --admin-listen ADDRESS  Listen on the given address for admin commands.\n");
	printf("                              The address must be in the same format as that\n");
	printf("                              of --listen. Default: " DEFAULT_LOGGING_AGENT_ADMIN_LISTEN_ADDRESS "\n");
	printf("      --authorize [LEVEL]:USERNAME:PASSWORDFILE\n");
	printf("                              Enables authentication on the admin server,\n");
	printf("                              through the given admin account. LEVEL indicates\n");
	printf("                              the privilege level (see below). PASSWORDFILE must\n");
	printf("                              point to a file containing the password\n");
	printf("\n");
	printf("      --dump-file PATH        Dump transactions without Union Station key to the\n");
	printf("                              following file. Default: /dev/null\n");
	printf("\n");
	printf("      --user USERNAME         Lower privilege to the given user. Only has\n");
	printf("                              effect when started as root\n");
	printf("      --group GROUPNAME       Lower privilege to the given group. Only has\n");
	printf("                              effect when started as root. Default: primary\n");
	printf("                              group of the username given by '--user'\n");
	printf("\n");
	printf("      --log-file PATH         Log to the given file.\n");
	printf("      --log-level LEVEL       Logging level. Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("\n");
	printf("  -h, --help                  Show this help\n");
	printf("\n");
	printf("Admin account privilege levels (ordered from most to least privileges):\n");
	printf("  readonly    Read-only access\n");
	printf("  full        Full access (default)\n");
}

inline bool
parseLoggingAgentOption(int argc, const char *argv[], int &i, VariantMap &options) {
	OptionParser p(loggingAgentUsage);

	if (p.isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
		options.set("passenger_root", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--password-file")) {
		options.set("logging_agent_password_file", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], 'l', "--listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			options.set("logging_agent_address", argv[i + 1]);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--admin-listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			vector<string> addresses = options.getStrSet("logging_agent_admin_addresses",
				false);
			if (addresses.size() == SERVER_KIT_MAX_SERVER_ENDPOINTS) {
				fprintf(stderr, "ERROR: you may specify up to %u --admin-listen addresses.\n",
					SERVER_KIT_MAX_SERVER_ENDPOINTS);
				exit(1);
			}
			addresses.push_back(argv[i + 1]);
			options.setStrSet("logging_agent_admin_addresses", addresses);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --admin-listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--authorize")) {
		vector<string> args;
		vector<string> authorizations = options.getStrSet("logging_agent_authorizations",
				false);

		split(argv[i + 1], ':', args);
		if (args.size() < 2 || args.size() > 3) {
			fprintf(stderr, "ERROR: invalid format for --authorize. The syntax "
				"is \"[LEVEL:]USERNAME:PASSWORDFILE\".\n");
			exit(1);
		}

		authorizations.push_back(argv[i + 1]);
		options.setStrSet("logging_agent_authorizations", authorizations);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--dump-file")) {
		options.set("analytics_dump_file", argv[i + 1]);
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
		options.setInt("logging_agent_log_level", atoi(argv[i + 1]));
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-file")) {
		// We do not set debug_log_file because, when this function is called from
		// the Watchdog, we don't want to affect the Watchdog's own log file.
		options.set("logging_agent_log_file", argv[i + 1]);
		i += 2;
	} else {
		return false;
	}
	return true;
}


} // namespace Passenger

#endif /* _PASSENGER_LOGGING_AGENT_OPTION_PARSER_H_ */

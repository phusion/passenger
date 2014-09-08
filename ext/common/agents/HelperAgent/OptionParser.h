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
#ifndef _PASSENGER_SERVER_OPTION_PARSER_H_
#define _PASSENGER_SERVER_OPTION_PARSER_H_

#include <cstdio>
#include <Constants.h>
#include <Utils/VariantMap.h>
#include <Utils/OptionParsing.h>

namespace Passenger {

using namespace std;


inline void
serverUsage() {
	printf("Usage: PassengerAgent server <OPTIONS...> [APP DIRECTORY]\n");
	printf("Runs the " PROGRAM_NAME " standalone HTTP server agent.\n");
	printf("\n");
	printf("The server starts in single-app mode, unless --multi-app is specified. When\n");
	printf("in single-app mode, it serves the app at the current working directory, or the\n");
	printf("app specified by APP DIRECTORY.\n");
	printf("\n");
	printf("Required options:\n");
	printf("       --passenger-root PATH  The location to the " PROGRAM_NAME " source\n");
	printf("                              directory\n");
	printf("\n");
	printf("Socket options (optional):\n");
	printf("  -l,  --listen ADDRESS     Listen on the given address. The address must be\n");
	printf("                            formatted as tcp://IP:PORT for TCP sockets, or\n");
	printf("                            unix:PATH for Unix domain sockets. You can specify\n");
	printf("                            this option multiple times (up to %u times) to\n",
		SERVER_KIT_MAX_SERVER_ENDPOINTS);
	printf("                            listen on multiple addresses. Default:\n");
	printf("                            " DEFAULT_HTTP_SERVER_LISTEN_ADDRESS "\n");
	printf("\n");
	printf("Application serving options (optional):\n");
	printf("  -e, --environment NAME    Default framework environment name to use.\n");
	printf("                            Default: " DEFAULT_APP_ENV "\n");
	printf("      --app-type TYPE       The type of application you want to serve\n");
	printf("                            (single-app mode only)\n");
	printf("      --startup-file PATH   The path of the app's startup file, relative to\n");
	printf("                            the app root directory (single-app mode only)\n");
	printf("\n");
	printf("      --multi-app           Enable multi-app mode\n");
	printf("\n");
	printf("Process management options (optional):\n");
	printf("      --max-pool-size N     Maximum number of application processes.\n");
	printf("                            Default: %d\n", DEFAULT_MAX_POOL_SIZE);
	printf("      --pool-idle-time SECS  Maximum number of seconds an application process\n");
	printf("                             may be idle. Default: %d\n", DEFAULT_POOL_IDLE_TIME);
	printf("      --min-instances N     Minimum number of application processes. Default: 1\n");
	printf("\n");
	printf("Other options (optional):\n");
	printf("      --log-level LEVEL     Logging level. Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("  -h, --help                Show this help\n");
}

inline bool
parseServerOption(int argc, const char *argv[], int &i, VariantMap &options) {
	OptionParser p(serverUsage);

	if (p.isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
		options.set("passenger_root", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], 'l', "--listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			vector<string> addresses = options.getStrSet("listen", false);
			if (addresses.size() == SERVER_KIT_MAX_SERVER_ENDPOINTS) {
				fprintf(stderr, "ERROR: you may specify up to %u --listen addresses.\n",
					SERVER_KIT_MAX_SERVER_ENDPOINTS);
				exit(1);
			}
			addresses.push_back(argv[i + 1]);
			options.setStrSet("server_listen_addresses", addresses);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--max-pool-size")) {
		options.setInt("max_pool_size", atoi(argv[i + 1]));
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--pool-idle-time")) {
		options.setInt("pool_idle_time", atoi(argv[i + 1]));
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--min-instances")) {
		options.setInt("min_instances", atoi(argv[i + 1]));
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], 'e', "--environment")) {
		options.set("environment", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--app-type")) {
		options.set("app_type", argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--startup-file")) {
		options.set("startup_file", argv[i + 1]);
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--multi-app")) {
		options.setBool("multi_app", true);
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-level")) {
		// We do not set log_level because, when this function is called from
		// the Watchdog, we don't want to affect the Watchdog's own log level.
		options.setInt("server_log_level", atoi(argv[i + 1]));
		i += 2;
	} else if (!startsWith(argv[i], "-")) {
		if (!options.has("app_root")) {
			options.set("app_root", argv[i]);
			i++;
		} else {
			fprintf(stderr, "ERROR: you may not pass multiple application directories. "
				"Please type '%s server --help' for usage.\n", argv[0]);
			exit(1);
		}
	} else {
		return false;
	}
	return true;
}

void
parseServerOptions(int argc, const char *argv[], int start, VariantMap &options) {
	int i = start;

	while (i < argc) {
		if (!parseServerOption(argc, argv, i, options)) {
			fprintf(stderr, "ERROR: unrecognized argument %s. Please type "
				"'%s server --help' for usage.\n", argv[i], argv[0]);
			exit(1);
		}
	}
}


} // namespace Passenger

#endif /* _PASSENGER_SERVER_OPTION_PARSER_H_ */

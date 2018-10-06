/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_CORE_OPTION_PARSER_H_
#define _PASSENGER_CORE_OPTION_PARSER_H_

#include <boost/thread.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <Constants.h>
#include <JsonTools/Autocast.h>
#include <Utils.h>
#include <Utils/OptionParsing.h>
#include <StrIntTools/StrIntUtils.h>

#include <jsoncpp/json.h>

namespace Passenger {

using namespace std;


inline void
coreUsage() {
	// ....|---------------Keep output within standard terminal width (80 chars)------------|
	printf("Usage: " AGENT_EXE " core <OPTIONS...> [APP DIRECTORY]\n");
	printf("Runs the " PROGRAM_NAME " core.\n");
	printf("\n");
	printf("The core starts in single-app mode, unless --multi-app is specified. When\n");
	printf("in single-app mode, it serves the app at the current working directory, or the\n");
	printf("app specified by APP DIRECTORY.\n");
	printf("\n");
	printf("Required options:\n");
	printf("      --passenger-root PATH  The location to the " PROGRAM_NAME " source\n");
	printf("                             directory\n");
	printf("\n");
	printf("Socket options (optional):\n");
	printf("  -l, --listen ADDRESS      Listen on the given address. The address must be\n");
	printf("                            formatted as tcp://IP:PORT for TCP sockets, or\n");
	printf("                            unix:PATH for Unix domain sockets. You can specify\n");
	printf("                            this option multiple times (up to %u times) to\n",
		SERVER_KIT_MAX_SERVER_ENDPOINTS);
	printf("                            listen on multiple addresses. Default:\n");
	printf("                            " DEFAULT_HTTP_SERVER_LISTEN_ADDRESS "\n");
	printf("      --api-listen ADDRESS  Listen on the given address for API commands.\n");
	printf("                            The same syntax and limitations as with --listen\n");
	printf("                            are applicable\n");
	printf("      --socket-backlog      Override size of the socket backlog.\n");
	printf("                            Default: %d\n", DEFAULT_SOCKET_BACKLOG);
	printf("\n");
	printf("Daemon options (optional):\n");
	printf("      --pid-file PATH       Store the core's PID in the given file. The file\n");
	printf("                            is deleted on exit\n");
	printf("\n");
	printf("Security options (optional):\n");
	printf("      --multi-app-password-file PATH\n");
	printf("                            Password-protect access to the core's HTTP server\n");
	printf("                            (multi-app mode only)\n");
	printf("      --authorize [LEVEL]:USERNAME:PASSWORDFILE\n");
	printf("                            Enables authentication on the API server, through\n");
	printf("                            the given API account. LEVEL indicates the\n");
	printf("                            privilege level (see below). PASSWORDFILE must\n");
	printf("                            point to a file containing the password\n");
	printf("      --no-user-switching   Disables user switching support\n");
	printf("      --default-user NAME   Default user to start apps as, when user\n");
	printf("                            switching is enabled. Default: " DEFAULT_WEB_APP_USER "\n");
	printf("      --default-group NAME  Default group to start apps as, when user\n");
	printf("                            switching is disabled. Default: the default\n");
	printf("                            user's primary group\n");
	printf("      --disable-security-update-check\n");
	printf("                            Disable the periodic check and notice about\n");
	printf("                            important security updates\n");
	printf("      --security-update-check-proxy PROXY\n");
	printf("                            Use HTTP/SOCKS proxy for the security update check:\n");
	printf("                            scheme://user:password@proxy_host:proxy_port\n");
	printf("      --disable-anonymous-telemetry\n");
	printf("                            Disable anonymous telemetry collection\n");
	printf("      --anonymous-telemetry-proxy PROXY\n");
	printf("                            Use HTTP/SOCKS proxy for anonymous telemetry sending:\n");
	printf("                            scheme://user:password@proxy_host:proxy_port\n");
	printf("\n");
	printf("Application serving options (optional):\n");
	printf("  -e, --environment NAME    Default framework environment name to use.\n");
	printf("                            Default: " DEFAULT_APP_ENV "\n");
	printf("      --app-type TYPE       The type of application you want to serve\n");
	printf("                            (single-app mode only)\n");
	printf("      --startup-file PATH   The path of the app's startup file, relative to\n");
	printf("                            the app root directory (single-app mode only)\n");
	printf("      --spawn-method NAME   Spawn method to use. Can either be 'smart' or\n");
	printf("                            'direct'. Default: %s\n", DEFAULT_SPAWN_METHOD);
	printf("      --load-shell-envvars  Load shell startup files before loading application\n");
	printf("      --concurrency-model   The concurrency model to use for the app, either\n");
	printf("                            'process' or 'thread' (Enterprise only).\n");
	printf("                            Default: " DEFAULT_CONCURRENCY_MODEL "\n");
	printf("      --app-thread-count    The number of application threads to use when using\n");
	printf("                            the 'thread' concurrency model (Enterprise only).\n");
	printf("                            Default: %d\n", DEFAULT_APP_THREAD_COUNT);
	printf("\n");
	printf("      --multi-app           Enable multi-app mode\n");
	printf("\n");
	printf("      --force-friendly-error-pages\n");
	printf("                            Force friendly error pages to be always on\n");
	printf("      --disable-friendly-error-pages\n");
	printf("                            Force friendly error pages to be always off\n");
	printf("\n");
	printf("      --ruby PATH           Default Ruby interpreter to use.\n");
	printf("      --nodejs PATH         Default NodeJs interpreter to use.\n");
	printf("      --python PATH         Default Python interpreter to use.\n");
	printf("      --meteor-app-settings PATH\n");
	printf("                            File with settings for a Meteor (non-bundled) app.\n");
	printf("                            (passed to Meteor using --settings)\n");
	printf("      --app-file-descriptor-ulimit NUMBER\n");
	printf("                            Set custom file descriptor ulimit for the app\n");
	printf("      --debugger            Enable Ruby debugger support (Enterprise only)\n");
	printf("\n");
	printf("      --rolling-restarts    Enable rolling restarts (Enterprise only)\n");
	printf("      --resist-deployment-errors\n");
	printf("                            Enable deployment error resistance (Enterprise only)\n");
	printf("\n");
	printf("Process management options (optional):\n");
	printf("      --max-pool-size N     Maximum number of application processes.\n");
	printf("                            Default: %d\n", DEFAULT_MAX_POOL_SIZE);
	printf("      --pool-idle-time SECS\n");
	printf("                            Maximum number of seconds an application process\n");
	printf("                            may be idle. Default: %d\n", DEFAULT_POOL_IDLE_TIME);
	printf("      --max-preloader-idle-time SECS\n");
	printf("                            Maximum time that preloader processes may be\n");
	printf("                            be idle. A value of 0 means that preloader\n");
	printf("                            processes never timeout. Default: %d\n", DEFAULT_MAX_PRELOADER_IDLE_TIME);
	printf("      --force-max-concurrent-requests-per-process NUMBER\n");
	printf("                            Force " SHORT_PROGRAM_NAME " to believe that an application\n");
	printf("                            process can handle the given number of concurrent\n");
	printf("                            requests per process\n");
	printf("      --min-instances N     Minimum number of application processes. Default: 1\n");
	printf("      --memory-limit MB     Restart application processes that go over the\n");
	printf("                            given memory limit (Enterprise only)\n");
	printf("\n");
	printf("Request handling options (optional):\n");
	printf("      --max-requests        Restart application processes that have handled\n");
	printf("                            the specified maximum number of requests\n");
	printf("      --max-request-time    Abort requests that take too much time (Enterprise\n");
	printf("                            only)\n");
	printf("      --max-request-queue-size NUMBER\n");
	printf("                            Specify request queue size. Default: %d\n",
		DEFAULT_MAX_REQUEST_QUEUE_SIZE);
	printf("      --sticky-sessions     Enable sticky sessions\n");
	printf("      --sticky-sessions-cookie-name NAME\n");
	printf("                            Cookie name to use for sticky sessions.\n");
	printf("                            Default: " DEFAULT_STICKY_SESSIONS_COOKIE_NAME "\n");
	printf("      --vary-turbocache-by-cookie NAME\n");
	printf("                            Vary the turbocache by the cookie of the given name\n");
	printf("      --disable-turbocaching\n");
	printf("                            Disable turbocaching\n");
	printf("      --no-abort-websockets-on-process-shutdown\n");
	printf("                            Do not abort WebSocket connections on process\n");
	printf("                            shutdown or restart\n");
	printf("\n");
	printf("Other options (optional):\n");
	printf("      --log-file PATH       Log to the given file.\n");
	printf("      --log-level LEVEL     Logging level. Default: %d\n", DEFAULT_LOG_LEVEL);
	printf("      --fd-log-file PATH    Log file descriptor activity to the given file.\n");
	printf("      --stat-throttle-rate SECONDS\n");
	printf("                            Throttle filesystem restart.txt checks to at most\n");
	printf("                            once per given seconds. Default: %d\n", DEFAULT_STAT_THROTTLE_RATE);
	printf("      --no-show-version-in-header\n");
	printf("                            Do not show " PROGRAM_NAME " version number in\n");
	printf("                            HTTP headers.\n");
	printf("      --data-buffer-dir PATH\n");
	printf("                            Directory to store data buffers in. Default:\n");
	printf("                            %s\n", getSystemTempDir());
	printf("      --no-graceful-exit    When exiting, exit immediately instead of waiting\n");
	printf("                            for all connections to terminate\n");
	printf("      --benchmark MODE      Enable benchmark mode. Available modes:\n");
	printf("                            after_accept,before_checkout,after_checkout,\n");
	printf("                            response_begin\n");
	printf("      --disable-selfchecks  Disable various self-checks. This improves\n");
	printf("                            performance, but might delay finding bugs in\n");
	printf("                            " PROGRAM_NAME "\n");
	printf("      --threads NUMBER      Number of threads to use for request handling.\n");
	printf("                            Default: number of CPU cores (%d)\n",
		boost::thread::hardware_concurrency());
	printf("      --cpu-affine          Enable per-thread CPU affinity (Linux only)\n");
	printf("      --core-file-descriptor-ulimit NUMBER\n");
	printf("                            Set custom file descriptor ulimit for the core\n");
	printf("      --admin-panel-url URL\n");
	printf("                            Connect to an admin panel through this service\n");
	printf("                            connector URL\n");
	printf("      --ctl NAME=VALUE      Set low-level config option directly\n");
	printf("  -h, --help                Show this help\n");
	printf("\n");
	printf("API account privilege levels (ordered from most to least privileges):\n");
	printf("  readonly    Read-only access\n");
	printf("  full        Full access (default)\n");
}

inline bool
parseCoreOption(int argc, const char *argv[], int &i, Json::Value &updates) {
	OptionParser p(coreUsage);

	if (p.isValueFlag(argc, i, argv[i], '\0', "--passenger-root")) {
		updates["passenger_root"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], 'l', "--listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			Json::Value &addresses = updates["controller_addresses"];
			if (addresses.size() == SERVER_KIT_MAX_SERVER_ENDPOINTS) {
				fprintf(stderr, "ERROR: you may specify up to %u --listen addresses.\n",
					SERVER_KIT_MAX_SERVER_ENDPOINTS);
				exit(1);
			}
			addresses.append(argv[i + 1]);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--api-listen")) {
		if (getSocketAddressType(argv[i + 1]) != SAT_UNKNOWN) {
			Json::Value &addresses = updates["api_server_addresses"];
			if (addresses.size() == SERVER_KIT_MAX_SERVER_ENDPOINTS) {
				fprintf(stderr, "ERROR: you may specify up to %u --api-listen addresses.\n",
					SERVER_KIT_MAX_SERVER_ENDPOINTS);
				exit(1);
			}
			addresses.append(argv[i + 1]);
			i += 2;
		} else {
			fprintf(stderr, "ERROR: invalid address format for --api-listen. The address "
				"must be formatted as tcp://IP:PORT for TCP sockets, or unix:PATH "
				"for Unix domain sockets.\n");
			exit(1);
		}
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--pid-file")) {
		updates["pid_file"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--authorize")) {
		vector<string> args;
		split(argv[i + 1], ':', args);
		if (args.size() < 2 || args.size() > 3) {
			fprintf(stderr, "ERROR: invalid format for --authorize. The syntax "
				"is \"[LEVEL:]USERNAME:PASSWORDFILE\".\n");
			exit(1);
		}

		updates["api_server_authorizations"].append(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--socket-backlog")) {
		updates["controller_socket_backlog"] = argv[i + 1];
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--no-user-switching")) {
		updates["user_switching"] = false;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--default-user")) {
		updates["default_user"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--default-group")) {
		updates["default_group"] = argv[i + 1];
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--disable-security-update-check")) {
		updates["security_update_checker_disabled"] = true;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--security-update-check-proxy")) {
		updates["security_update_checker_proxy_url"] = argv[i + 1];
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--disable-anonymous-telemetry")) {
		updates["telemetry_collector_disabled"] = true;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--anonymous-telemetry-proxy")) {
		updates["telemetry_collector_proxy_url"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--max-pool-size")) {
		updates["max_pool_size"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--pool-idle-time")) {
		updates["pool_idle_time"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--max-preloader-idle-time")) {
		updates["default_max_preloader_idle_time"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--force-max-concurrent-requests-per-process")) {
		updates["default_force_max_concurrent_requests_per_process"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--min-instances")) {
		updates["default_min_instances"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], 'e', "--environment")) {
		updates["default_environment"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--app-type")) {
		updates["single_app_mode_app_type"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--startup-file")) {
		updates["single_app_mode_startup_file"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--spawn-method")) {
		updates["default_spawn_method"] = argv[i + 1];
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--load-shell-envvars")) {
		updates["default_load_shell_envvars"] = true;
		i++;
	} else if (p.isFlag(argv[i], '\0', "--multi-app")) {
		updates["multi_app"] = true;
		i++;
	} else if (p.isFlag(argv[i], '\0', "--force-friendly-error-pages")) {
		updates["default_friendly_error_pages"] = true;
		i++;
	} else if (p.isFlag(argv[i], '\0', "--disable-friendly-error-pages")) {
		updates["default_friendly_error_pages"] = false;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--max-requests")) {
		updates["default_max_requests"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--max-request-queue-size")) {
		updates["default_max_request_queue_size"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--sticky-sessions")) {
		updates["default_sticky_sessions"] = true;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--sticky-sessions-cookie-name")) {
		updates["default_sticky_sessions_cookie_name"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--vary-turbocache-by-cookie")) {
		updates["vary_turbocache_by_cookie"] = argv[i + 1];
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--disable-turbocaching")) {
		updates["turbocaching"] = false;
		i++;
	} else if (p.isFlag(argv[i], '\0', "--no-abort-websockets-on-process-shutdown")) {
		updates["default_abort_websockets_on_process_shutdown"] = false;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--ruby")) {
		updates["default_ruby"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--nodejs")) {
		updates["default_nodejs"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--python")) {
		updates["default_python"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--meteor-app-settings")) {
		updates["default_meteor_app_settings"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--app-file-descriptor-ulimit")) {
		updates["default_app_file_descriptor_ulimit"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-level")) {
		updates["log_level"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--log-file")) {
		updates["log_target"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--fd-log-file")) {
		updates["file_descriptor_log_target"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--stat-throttle-rate")) {
		updates["stat_throttle_rate"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--no-show-version-in-header")) {
		updates["show_version_in_header"] = false;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--data-buffer-dir")) {
		updates["controller_file_buffered_channel_buffer_dir"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--no-graceful-exit")) {
		updates["graceful_exit"] = false;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--benchmark")) {
		updates["benchmark_mode"] = argv[i + 1];
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--disable-selfchecks")) {
		updates["pool_selfchecks"] = false;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--threads")) {
		updates["controller_threads"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isFlag(argv[i], '\0', "--cpu-affine")) {
		updates["controller_cpu_affine"] = true;
		i++;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--core-file-descriptor-ulimit")) {
		updates["file_descriptor_ulimit"] = atoi(argv[i + 1]);
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--admin-panel-url")) {
		updates["admin_panel_url"] = argv[i + 1];
		i += 2;
	} else if (p.isValueFlag(argc, i, argv[i], '\0', "--ctl")) {
		const char *sep = strchr(argv[i + 1], '=');
		if (sep == NULL) {
			fprintf(stderr, "ERROR: invalid --ctl format: %s\n", argv[i + 1]);
			exit(1);
		}
		string name(argv[i + 1], sep - argv[i + 1]);
		string value(sep + 1);
		updates[name] = autocastValueToJson(value);
		i += 2;
	} else if (!startsWith(argv[i], "-")) {
		if (!updates.isMember("single_app_mode_app_root")) {
			updates["single_app_mode_app_root"] = argv[i];
			i++;
		} else {
			fprintf(stderr, "ERROR: you may not pass multiple application directories. "
				"Please type '%s core --help' for usage.\n", argv[0]);
			exit(1);
		}
	} else {
		return false;
	}
	return true;
}


} // namespace Passenger

#endif /* _PASSENGER_CORE_OPTION_PARSER_H_ */

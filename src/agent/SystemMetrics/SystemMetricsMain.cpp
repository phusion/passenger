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
#include <iostream>
#include <string>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include <SystemTools/SystemMetricsCollector.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>

using namespace std;
using namespace Passenger;

namespace {
	struct Options {
		bool xml;
		SystemMetrics::XmlOptions xmlOptions;
		SystemMetrics::DescriptionOptions descOptions;
		int interval;
		bool useStdin;
		bool exitOnUnexpectedError;
		bool help;

		Options() {
			xml = false;
			descOptions.colors = isatty(1);
			interval = -1;
			useStdin = false;
			exitOnUnexpectedError = true;
			help = false;
		}
	};
}

static bool
isFlag(const char *arg, char shortFlagName, const char *longFlagName) {
	return strcmp(arg, longFlagName) == 0
		|| (shortFlagName != '\0' && arg[0] == '-'
			&& arg[1] == shortFlagName && arg[2] == '\0');
}

static void
usage() {
	printf("Usage: passenger-config system-metrics [OPTIONS]\n");
	printf("Displays various metrics about the system.\n");
	printf("\n");
	printf("Options:\n");
	printf("        --xml              Output in XML format\n");
	printf("        --no-general       Do not display general metrics\n");
	printf("        --no-cpu           Do not display CPU metrics\n");
	printf("        --no-memory        Do not display memory metrics\n");
	printf("        --force-colors     Display colors even if stdout is not a TTY\n");
	printf("    -w  --watch INTERVAL   Reprint metrics every INTERVAL seconds\n");
	printf("        --stdin            Reprint metrics every time a newline is received on\n");
	printf("                           stdin, until EOF. Mutually exclusive with --watch\n");
	printf("        --no-exit-on-unexpected-error   Normally, if an unexpected error is\n");
	printf("                           encountered while collecting system metrics, this\n");
	printf("                           program will exit with an error code. This option\n");
	printf("                           suppresses that\n");
	printf("    -h, --help             Show this help\n");
}

static Options
parseOptions(int argc, char *argv[]) {
	Options options;
	int i = 2;

	while (i < argc) {
		if (isFlag(argv[i], '\0', "--xml")) {
			options.xml = true;
			i++;
		} else if (isFlag(argv[i], '\0', "--no-general")) {
			options.xmlOptions.general = false;
			options.descOptions.general = false;
			i++;
		} else if (isFlag(argv[i], '\0', "--no-cpu")) {
			options.xmlOptions.cpu = false;
			options.descOptions.cpu = false;
			i++;
		} else if (isFlag(argv[i], '\0', "--no-memory")) {
			options.xmlOptions.memory = false;
			options.descOptions.memory = false;
			i++;
		} else if (isFlag(argv[i], '\0', "--force-colors")) {
			options.descOptions.colors = true;
			i++;
		} else if (isFlag(argv[i], 'w', "--watch")) {
			if (argc >= i + 2) {
				options.interval = atoi(argv[i + 1]);
				i += 2;
			} else {
				fprintf(stderr, "ERROR: extra argument required for --watch\n");
				usage();
				exit(1);
			}
		} else if (isFlag(argv[i], '\0', "--stdin")) {
			options.useStdin = true;
			i++;
		} else if (isFlag(argv[i], '\0', "--no-exit-on-unexpected-error")) {
			options.exitOnUnexpectedError = false;
			i++;
		} else if (isFlag(argv[i], 'h', "--help")) {
			options.help = true;
			i++;
		} else {
			fprintf(stderr, "ERROR: unrecognized argument %s\n", argv[i]);
			usage();
			exit(1);
		}
	}
	if (options.interval != -1 && options.useStdin) {
		fprintf(stderr, "ERROR: --watch and --stdin are mutually exclusive.\n");
		exit(1);
	}
	return options;
}

static bool
waitForNextLine() {
	char buf[1024];
	return fgets(buf, sizeof(buf), stdin) != NULL;
}

static void
perform(const Options &options, SystemMetricsCollector &collector, SystemMetrics &metrics) {
	try {
		collector.collect(metrics);
		if (options.xml) {
			cout << "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
			metrics.toXml(cout, options.xmlOptions);
			cout << endl;
		} else {
			metrics.toDescription(cout, options.descOptions);
		}
	} catch (const RuntimeException &e) {
		fprintf(stderr, "An error occurred while collecting system metrics: %s\n", e.what());
		if (options.exitOnUnexpectedError) {
			exit(1);
		}
	}
}

int
systemMetricsMain(int argc, char *argv[]) {
	Options options = parseOptions(argc, argv);
	if (options.help) {
		usage();
		return 0;
	}

	SystemMetricsCollector collector;
	SystemMetrics metrics;

	if (options.descOptions.cpu) {
		collector.collect(metrics);
		// We have to measure system metrics within an interval
		// in order to determine the CPU usage.
		usleep(50000);
	}

	if (options.useStdin) {
		while (waitForNextLine()) {
			perform(options, collector, metrics);
		}
	} else {
		do {
			perform(options, collector, metrics);
			if (options.interval != -1) {
				sleep(options.interval);
			}
		} while (options.interval != -1);
	}
	return 0;
}

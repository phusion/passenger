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
#ifndef _PASSENGER_SYSTEM_METRICS_COLLECTOR_H_
#define _PASSENGER_SYSTEM_METRICS_COLLECTOR_H_

#include <boost/cstdint.hpp>
#include <boost/thread.hpp>
#include <boost/typeof/typeof.hpp>
#include <ostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cmath>
#include <ctime>
#include <sys/types.h>
#include <sys/utsname.h>
#ifdef __linux__
	#include <sys/sysinfo.h>
	#include <Exceptions.h>
	#include <FileTools/FileManip.h>
	#include <StrIntTools/StringScanning.h>
#endif
#ifdef __APPLE__
	#include <mach/mach.h>
	#include <sys/sysctl.h>
	#include <sys/time.h>
	#include <Availability.h>
#endif
#ifdef __FreeBSD__
	#include <sys/param.h>
	#include <sys/sysctl.h>
	#include <sys/resource.h>
	#include <vm/vm_param.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <Constants.h>
#include <StaticString.h>
#include <Utils.h>
#include <StrIntTools/StrIntUtils.h>
#include <SystemTools/SystemTime.h>
#include <Utils/AnsiColorConstants.h>
#include <Utils/SpeedMeter.h>

/*
 * Useful resources
 *
 * OS X:
 * http://www.opensource.apple.com/source/system_cmds/system_cmds-496/iostat.tproj/iostat.c
 * https://github.com/max-horvath/htop-osx
 * https://github.com/malkia/busybox-osx/blob/master/procps/iostat.c
 *
 * Linux:
 * http://procps.cvs.sourceforge.net/viewvc/procps/procps/
 * https://github.com/sysstat/sysstat/blob/master/mpstat.c
 * http://www.thomas-krenn.com/en/wiki/Linux_Performance_Measurements_using_vmstat
 * http://man7.org/linux/man-pages/man5/proc.5.html
 *
 * FreeBSD:
 * https://github.com/freebsd/freebsd/blob/master/usr.bin/vmstat/vmstat.c
 * https://github.com/freebsd/freebsd/blob/master/sbin/swapon/swapon.c
 * http://stuff.mit.edu/afs/sipb/project/freebsd/head/contrib/top/machine.h
 */

namespace Passenger {

using namespace boost;
using namespace std;

class SystemMetricsCollector;

/** All memory sizes are in KB. */
class SystemMetrics {
public:
	struct DescriptionOptions {
		bool general;
		bool cpu;
		bool memory;
		bool colors;

		DescriptionOptions()
			: general(true),
			  cpu(true),
			  memory(true),
			  colors(false)
			{ }
	};

	struct XmlOptions {
		bool general;
		bool cpu;
		bool memory;

		XmlOptions()
			: general(true),
			  cpu(true),
			  memory(true)
			{ }
	};

private:
	friend class SystemMetricsCollector;

	#ifdef __linux__
		SpeedMeter<unsigned long long, 8, 1000000, 60 * 1000000, 1000000> forkRateSpeedMeter;
		SpeedMeter<size_t, 8, 1000000, 60 * 1000000, 1000000> swapInSpeedMeter, swapOutSpeedMeter;
	#endif

	double divideTotalCpuUsageByNCpus(double total) const {
		if (ncpus() == 0) {
			return -1;
		} else {
			total /= ncpus();
			if (total > 100) {
				return 100;
			} else {
				return total;
			}
		}
	}

	void outputHeader(ostream &stream, const DescriptionOptions &options,
		const char *label) const
	{
		if (options.colors) {
			stream << ANSI_COLOR_BLUE_BG ANSI_COLOR_BOLD ANSI_COLOR_YELLOW;
		}
		stream << "------------- " << label << " -------------";
		if (options.colors) {
			stream << ANSI_COLOR_RESET;
		}
		stream << endl;
	}

	string formatWidth(const string &str, int width) const {
		char buf[128];
		snprintf(buf, sizeof(buf), "%*s", width, str.c_str());
		return buf;
	}

	string maybeColorAfterThreshold(const DescriptionOptions &options, const string &str,
		double value, double threshold) const
	{
		if (value >= threshold && options.colors) {
			return string(ANSI_COLOR_BOLD) + str + ANSI_COLOR_RESET;
		} else {
			return str;
		}
	}

	string formatPercent(const DescriptionOptions &options, double percent,
		int precision, int width, double threshold) const
	{
		string result;

		if (percent == -2) {
			if (options.colors) {
				return string(ANSI_COLOR_DGRAY)
					+ formatWidth("unsupported by OS", width)
					+ ANSI_COLOR_RESET;
			} else {
				return formatWidth("unsupported by OS", width);
			}
		} else if (percent == -1) {
			if (options.colors) {
				return string(ANSI_COLOR_RED)
					+ formatWidth("?", width)
					+ ANSI_COLOR_RESET;
			} else {
				return formatWidth("?", width);
			}
		} else {
			char buf[64];
			snprintf(buf, sizeof(buf), "%.*f%%", precision, percent);
			return maybeColorAfterThreshold(options, formatWidth(buf, width),
				percent, threshold);
		}
	}

	string formatPercent0(const DescriptionOptions &options, double percent,
		int width = -1, double threshold = numeric_limits<double>::infinity()) const
	{
		return formatPercent(options, percent, 0, width, threshold);
	}

	string formatPercent2(const DescriptionOptions &options, double percent,
		int width = -1, double threshold = -10) const
	{
		return formatPercent(options, percent, 2, width, threshold);
	}

	string kbToMb(ssize_t size) const {
		if (size < 0) {
			return "?";
		} else {
			char buf[32];
			snprintf(buf, sizeof(buf), "%lld", (long long) size / 1024);
			return buf;
		}
	}

public:
	class CpuUsage {
	private:
		friend class SystemMetricsCollector;

		unsigned long long lastUserTicks;
		unsigned long long lastNiceTicks;
		unsigned long long lastSystemTicks;
		unsigned long long lastIoWaitTicks;
		unsigned long long lastIdleTicks;
		unsigned long long lastStealTicks;

		/** Current usage statistics for this CPU.
		 *
		 * userUsage, niceUsage, systemUsage and idleUsage are fractions
		 * of user + nice + system + idle.
		 *
		 * ioWaitUsage is a fraction of user + nice + system + idle + iowait.
		 *
		 * stealUsage is a fraction of user + nice + system + idle + steal.
		 *
		 * All fractions range from 0 (unutilized) to SHRT_MAX (fully utilized).
		 * Use the *Pct() methods to convert them to percentages.
		 *
		 * Each statistic can individually be -1 if an error occurred while querying
		 * it, or -2 if the OS doesn't support it.
		 */
		short userUsage, niceUsage, systemUsage;
		short ioWaitUsage, idleUsage, stealUsage;

		double fracToPercentage(short usage) const {
			if (usage < 0) {
				return usage;
			} else {
				return (double) usage / SHRT_MAX * 100.0;
			}
		}

	public:
		CpuUsage()
			: lastUserTicks(0),
			  lastNiceTicks(0),
			  lastSystemTicks(0),
			  lastIoWaitTicks(0),
			  lastIdleTicks(0),
			  lastStealTicks(0),
			  userUsage(-1),
			  niceUsage(-1),
			  systemUsage(-1),
			  ioWaitUsage(-1),
			  idleUsage(-1),
			  stealUsage(-1)
			{ }

		/** These methods return the usage statistics as percentages (0..100) */

		double userPct() const {
			return fracToPercentage(userUsage);
		}

		double nicePct() const {
			return fracToPercentage(niceUsage);
		}

		double systemPct() const {
			return fracToPercentage(systemUsage);
		}

		double ioWaitPct() const {
			return fracToPercentage(ioWaitUsage);
		}

		double idlePct() const {
			return fracToPercentage(idleUsage);
		}

		double stealPct() const {
			return fracToPercentage(stealUsage);
		}

		/** Returns this CPU's usage as a percentage (0..100),
		 * or -1 if it cannot be determined.
		 */
		double usage() const {
			if (userUsage < 0 || niceUsage < 0 || systemUsage < 0) {
				return -1;
			} else {
				return userPct() + nicePct() + systemPct();
			}
		}
	};

	/** Per-core CPU usage. This collection is empty if the number of cores
	 * cannot be queried.
	 */
	vector<CpuUsage> cpuUsages;

	/** Total system physical RAM. -1 if this information cannot be queried. */
	ssize_t ramTotal;
	/** Amount of RAM used. Does not include kernel caches and buffers. -1
	 * if this information cannot be queried.
	 */
	ssize_t ramUsed;
	/** Total system swap space, or -1 if this information cannot be queried. */
	ssize_t swapTotal;
	/** Amount of swap space used, or -1 if this information cannot be queried. */
	ssize_t swapUsed;

	/** Load averages for the past 1, 5 and 15 minutes. Can each individually be -1
	 * if that particular statistic cannot be queried.
	 */
	double loadAverage1;
	double loadAverage5;
	double loadAverage15;

	/** Time at which the system booted. -1 if this information cannot be queried. */
	time_t boottime;

	/** Speed at which processes are created per second.
	 * SpeedMeter<unsigned long long>::unknownSpeed() if it's not yet known (because too
	 * few samples have been taken so far).
	 * -1 if there was an error querying this information.
	 * -2 if the OS does not support this metric.
	 */
	double forkRate;
	/** Speed at which the OS swaps in and swaps out data, in KB/sec.
	 * SpeedMeter<size_t>::unknownSpeed() if it's not yet known
	 * (because too few samples have been taken so far).
	 * -1 if there was an error querying this information.
	 * -2 if the OS does not support this metric.
	 */
	double swapInRate, swapOutRate;

	/** Kernel version number, or the empty string if this information cannot be queried. */
	string kernelVersion;

	SystemMetrics()
		:
		  #ifdef __linux__
		      forkRateSpeedMeter(),
		      swapInSpeedMeter(),
		      swapOutSpeedMeter(),
		  #endif
		  ramTotal(-1),
		  ramUsed(-1),
		  swapTotal(-1),
		  swapUsed(-1),
		  loadAverage1(-1),
		  loadAverage5(-1),
		  loadAverage15(-1),
		  boottime(-1),
		  forkRate(-2),
		  swapInRate(-2),
		  swapOutRate(-2)
		{ }

	unsigned int ncpus() const {
		return cpuUsages.size();
	}

	/** The following methods calculate the current average system CPU usage
	 * statistics. Ranges from 0 (no cores are being used) to 100 (all cores
	 * at full utilization). Return -1 if the information cannot be queried.
	 */

	double avgUserCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->userPct();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	double avgNiceCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->nicePct();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	double avgSystemCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->systemPct();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	double avgIoWaitCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->ioWaitPct();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	double avgIdleCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->idlePct();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	double avgStealCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->stealPct();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	double avgCpuUsage() const {
		BOOST_AUTO(it, cpuUsages.begin());
		BOOST_AUTO(end, cpuUsages.end());
		double total = 0;

		for (; it != end; it++) {
			double val = it->usage();
			if (val < 0) {
				return val;
			} else {
				total += val;
			}
		}
		return divideTotalCpuUsageByNCpus(total);
	}

	ssize_t ramFree() const {
		if (ramTotal == -1 || ramUsed == -1) {
			return -1;
		} else {
			return ramTotal - ramUsed;
		}
	}

	ssize_t swapFree() const {
		if (swapTotal == -1 || swapUsed == -1) {
			return -1;
		} else {
			return swapTotal - swapUsed;
		}
	}

	void toDescription(ostream &stream, const DescriptionOptions &options = DescriptionOptions()) const {
		char buf[1024];
		stream << std::right << std::setfill(' ');

		if (options.general) {
			outputHeader(stream, options, "General");
			stream << "Kernel version    : " << kernelVersion << endl;
			stream << "Uptime            : " << distanceOfTimeInWords(boottime) << endl;

			stream << "Load averages     : ";
			stream << formatPercent2(options, loadAverage1, 5, 2);
			stream << ", ";
			stream << formatPercent2(options, loadAverage5, 5, 2);
			stream << ", ";
			stream << formatPercent2(options, loadAverage15, 5, 2);
			stream << endl;

			if (forkRate != -2) {
				stream << "Fork rate         : ";
				if (forkRate == SpeedMeter<unsigned long long>::unknownSpeed() || forkRate < 0) {
					if (options.colors) {
						stream << ANSI_COLOR_DGRAY;
					}
					stream << "unknown";
					if (options.colors) {
						stream << ANSI_COLOR_RESET;
					}
				} else {
					char buf[32];

					snprintf(buf, sizeof(buf), "%.1f", forkRate);
					stream << buf << "/sec";
				}
				stream << endl;
			}

			stream << endl;
		}

		if (options.cpu) {
			double tmp;

			outputHeader(stream, options, "CPU");
			if (ncpus() == 0) {
				stream << "Number of CPUs    : unknown" << endl;
			} else {
				snprintf(buf, sizeof(buf), "%4u", ncpus());
				stream << "Number of CPUs    : " << buf << endl;
				stream << "Average CPU usage : ";
				stream << formatPercent0(options, avgCpuUsage(), 4, 95);
				stream << "  -- ";
				stream << formatPercent0(options, avgUserCpuUsage(), 4, 95);
				stream << " user, ";
				stream << formatPercent0(options, avgNiceCpuUsage(), 4, 95);
				stream << " nice, ";
				stream << formatPercent0(options, avgSystemCpuUsage(), 4, 95);
				stream << " system, ";
				stream << formatPercent0(options, avgIdleCpuUsage(), 4);
				stream << " idle" << endl;
			}

			for (unsigned i = 0; i < ncpus(); i++) {
				snprintf(buf, sizeof(buf),
					"  CPU %-2u          : ",
					i + 1);
				stream << buf;
				stream << formatPercent0(options, cpuUsages[i].usage(), 4, 95);
				stream << "  -- ";
				stream << formatPercent0(options, cpuUsages[i].userPct(), 4, 95);
				stream << " user, ";
				stream << formatPercent0(options, cpuUsages[i].nicePct(), 4, 95);
				stream << " nice, ";
				stream << formatPercent0(options, cpuUsages[i].systemPct(), 4, 95);
				stream << " system, ";
				stream << formatPercent0(options, cpuUsages[i].idlePct(), 4);
				stream << " idle" << endl;
			}

			// For the two average metrics below, if a metric is unsupported by the OS (-2)
			// then that implies that it's unsupported for all individual CPUs, so we
			// don't bother printing CPU-specific metrics.
			// But if an average metric is merely errored (-1), then it's still
			// possible that we succeeded in querying the metric for a specific CPU.

			tmp = avgIoWaitCpuUsage();
			if (tmp != -2) {
				stream << "I/O pressure      : ";
				stream << formatPercent0(options, avgIoWaitCpuUsage(), 4, 95);
				stream << endl;
				for (unsigned i = 0; i < ncpus(); i++) {
					snprintf(buf, sizeof(buf),
						"  CPU %-2u          : %s",
						i + 1,
						formatPercent0(options, cpuUsages[i].ioWaitPct(), 4, 95).c_str());
					stream << buf << endl;
				}
			}

			tmp = avgStealCpuUsage();
			if (tmp != -2) {
				stream << "Interference from other VMs: ";
				stream << formatPercent0(options, tmp, 4, 20);
				stream << endl;
				for (unsigned i = 0; i < ncpus(); i++) {
					snprintf(buf, sizeof(buf),
						"  CPU %-2u                   : %s",
						i + 1,
						formatPercent0(options, cpuUsages[i].stealPct(), 4, 35).c_str());
					stream << buf << endl;
				}
			}

			stream << endl;
		}

		if (options.memory) {
			double ramUsedPct = ramUsed / (double) ramTotal * 100;
			double swapUsedPct = swapUsed / (double) swapTotal * 100;
			outputHeader(stream, options, "Memory");
			stream << "RAM total         : " << formatWidth(kbToMb(ramTotal), 6) << " MB" << endl;
			stream << "RAM used          : " << formatWidth(kbToMb(ramUsed), 6) << " MB ("
				<< formatPercent0(options, ramUsedPct, 1, 90) << ")" << endl;
			stream << "RAM free          : " << formatWidth(kbToMb(ramFree()), 6) << " MB" << endl;
			stream << "Swap total        : " << formatWidth(kbToMb(swapTotal), 6) << " MB" << endl;
			stream << "Swap used         : " << formatWidth(kbToMb(swapUsed), 6) << " MB ("
				<< formatPercent0(options, swapUsedPct, 1, 90) << ")" << endl;
			stream << "Swap free         : " << formatWidth(kbToMb(swapFree()), 6) << " MB" << endl;

			if (swapInRate != -2) {
				stream << "Swap in           : ";
				if (swapInRate == SpeedMeter<size_t>::unknownSpeed() || swapInRate < 0) {
					if (options.colors) {
						stream << ANSI_COLOR_DGRAY;
					}
					stream << "unknown";
					if (options.colors) {
						stream << ANSI_COLOR_RESET;
					}
				} else {
					char buf[32];

					snprintf(buf, sizeof(buf), "%.1f", swapInRate / 1024);
					stream << maybeColorAfterThreshold(options, buf, swapInRate / 1024, 2);
					stream << " MB/sec";
				}
				stream << endl;
			}

			if (swapOutRate != -2) {
				stream << "Swap out          : ";
				if (swapOutRate == SpeedMeter<size_t>::unknownSpeed() || swapOutRate < 0) {
					if (options.colors) {
						stream << ANSI_COLOR_DGRAY;
					}
					stream << "unknown";
					if (options.colors) {
						stream << ANSI_COLOR_RESET;
					}
				} else {
					char buf[32];

					snprintf(buf, sizeof(buf), "%.1f", swapOutRate / 1024);
					stream << maybeColorAfterThreshold(options, buf, swapOutRate / 1024, 2);
					stream << " MB/sec";
				}
				stream << endl;
			}

			stream << endl;
		}
	}

	void toXml(ostream &stream, const XmlOptions &options = XmlOptions()) const {
		time_t timestamp = SystemTime::get();
		stream << std::fixed << std::setprecision(2);
		stream << "<system_metrics version=\"1.0\">";

		if (options.general) {
			stream << "<general>";
			stream << "<current_time>";
				stream << "<localtime>" << strip(std::ctime(&timestamp)) << "</localtime>";
				stream << "<timestamp>" << timestamp << "</timestamp>";
			stream << "</current_time>";
			stream << "<passenger_version>" PASSENGER_VERSION "</passenger_version>";
			stream << "<kernel_version>" << kernelVersion << "</kernel_version>";
			stream << "<boottime>";
				stream << "<localtime>" << strip(std::ctime(&boottime)) << "</localtime>";
				stream << "<timestamp>" << boottime << "</timestamp>";
			stream << "</boottime>";
			stream << "<uptime>";
				stream << "<seconds>" << timestamp - boottime << "</seconds>";
				stream << "<description>" << distanceOfTimeInWords(boottime) << "</description>";
			stream << "</uptime>";
			stream << "<load_averages>";
				stream << "<one>" << loadAverage1 << "</one>";
				stream << "<five>" << loadAverage5 << "</five>";
				stream << "<fifteen>" << loadAverage15 << "</fifteen>";
			stream << "</load_averages>";
			stream << "<fork_rate>" << forkRate << "</fork_rate>";
			stream << "</general>";
		}

		if (options.cpu) {
			stream << "<cpu_metrics>";
			stream << "<ncpus>" << (int) ncpus() << "</ncpus>";
			if (ncpus() != 0) {
				stream << "<average>";
					stream << "<usage>" << avgCpuUsage() << "</usage>";
					stream << "<user>" << avgUserCpuUsage() << "</user>";
					stream << "<nice>" << avgNiceCpuUsage() << "</nice>";
					stream << "<system>" << avgSystemCpuUsage() << "</system>";
					stream << "<iowait>" << avgIoWaitCpuUsage() << "</iowait>";
					stream << "<idle>" << avgIdleCpuUsage() << "</idle>";
					stream << "<steal>" << avgStealCpuUsage() << "</steal>";
				stream << "</average>";
			}
			stream << "<cpus>";
			for (unsigned i = 0; i < ncpus(); i++) {
				const CpuUsage &cpuUsage = cpuUsages[i];
				stream << "<cpu>";
				stream << "<number>" << i + 1 << "</number>";
				stream << "<usage>" << cpuUsage.usage() << "</usage>";
				stream << "<user>" << cpuUsage.userPct() << "</user>";
				stream << "<nice>" << cpuUsage.nicePct() << "</nice>";
				stream << "<system>" << cpuUsage.systemPct() << "</system>";
				stream << "<io_wait>" << cpuUsage.ioWaitPct() << "</io_wait>";
				stream << "<idle>" << cpuUsage.idlePct() << "</idle>";
				stream << "<steal>" << cpuUsage.stealPct() << "</steal>";
				stream << "</cpu>";
			}
			stream << "</cpus>";
			stream << "</cpu_metrics>";
		}

		if (options.memory) {
			stream << "<memory_metrics>";
			stream << "<ram_total>" << ramTotal << "</ram_total>";
			stream << "<ram_used>" << ramUsed << "</ram_used>";
			stream << "<ram_free>" << ramFree() << "</ram_free>";
			stream << "<swap_total>" << swapTotal << "</swap_total>";
			stream << "<swap_used>" << swapUsed << "</swap_used>";
			stream << "<swap_free>" << swapFree() << "</swap_free>";
			stream << "<swap_in_rate>" << swapInRate << "</swap_in_rate>";
			stream << "<swap_out_rate>" << swapOutRate << "</swap_out_rate>";
			stream << "</memory_metrics>";
		}

		stream << "</system_metrics>";
	}
};

/**
 * Utility class for collection system metrics, such as system CPU usage,
 * amount of memory available and free, etc.
 *
 *     SystemMetrics metrics;
 *     SystemMetricsCollector collector;
 *
 *     collector.collect(metrics);  // => metrics are now available
 *     sleep(1);
 *     collector.collect(metrics);  // => metrics have been updated
 *
 * Note that to measure the CPU usage, you must collect metrics at least
 * twice, using the same metrics object, within a time interval that's
 * longer than 10 ms. That's because on most systems, the CPU usage is
 * measured by comparing the number of CPU ticks that have passed at the
 * beginning and end of a time interval. The metrics object remembers the
 * number of CPU ticks that was queried last time.
 */
class SystemMetricsCollector {
private:
	#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
		int pageSize;

		void queryLoadAvg(SystemMetrics &metrics) const {
			double avg[3];
			int ret;

			ret = getloadavg(avg, 3);
			if (ret >= 1) {
				metrics.loadAverage1 = avg[0];
			}
			if (ret >= 2) {
				metrics.loadAverage5 = avg[1];
			}
			if (ret >= 3) {
				metrics.loadAverage15 = avg[2];
			}
		}

		void failReadingCpuUsages(SystemMetrics &metrics) const {
			vector<SystemMetrics::CpuUsage>::iterator it, end = metrics.cpuUsages.end();

			for (it = metrics.cpuUsages.begin(); it != end; it++) {
				it->userUsage = -1;
				it->niceUsage = -1;
				it->systemUsage = -1;
				it->idleUsage = -1;
				#if defined(__linux__)
					it->ioWaitUsage = -1;
					it->stealUsage  = -1;
				#else
					it->ioWaitUsage = -2;
					it->stealUsage  = -2;
				#endif
			}
		}

		short fracToShort(double x) const {
			return (short) (x * SHRT_MAX);
		}

		void updateCpuMetrics(SystemMetrics::CpuUsage &cpuUsage, long long user, long long nice,
			long long sys, long long iowait, long long idle, long long steal) const
		{
			unsigned long long userDiff, niceDiff, systemDiff, ioWaitDiff,
				idleDiff, stealDiff;
			double totalCalculationTicks, totalTicks;

			userDiff   = (unsigned long long) user - cpuUsage.lastUserTicks;
			niceDiff   = (unsigned long long) nice - cpuUsage.lastNiceTicks;
			systemDiff = (unsigned long long) sys  - cpuUsage.lastSystemTicks;
			if (iowait >= 0) {
				ioWaitDiff = (unsigned long long) iowait - cpuUsage.lastIoWaitTicks;
			}
			idleDiff   = (unsigned long long) idle - cpuUsage.lastIdleTicks;
			if (steal >= 0) {
				stealDiff  = (unsigned long long) steal - cpuUsage.lastStealTicks;
			}

			totalCalculationTicks = userDiff;
			totalCalculationTicks += niceDiff;
			totalCalculationTicks += systemDiff;
			totalCalculationTicks += idleDiff;
			if (totalCalculationTicks == 0) {
				// If the CPU didn't tick, treat it as 100% idle.
				cpuUsage.userUsage   = 0;
				cpuUsage.niceUsage   = 0;
				cpuUsage.systemUsage = 0;
				cpuUsage.idleUsage   = fracToShort(1);
			} else {
				cpuUsage.userUsage   = fracToShort(userDiff / totalCalculationTicks);
				cpuUsage.niceUsage   = fracToShort(niceDiff / totalCalculationTicks);
				cpuUsage.systemUsage = fracToShort(systemDiff / totalCalculationTicks);
				cpuUsage.idleUsage   = fracToShort(idleDiff / totalCalculationTicks);
			}

			if (iowait >= 0) {
				totalTicks = totalCalculationTicks + ioWaitDiff;
				if (totalTicks == 0) {
					cpuUsage.ioWaitUsage = 0;
				} else {
					cpuUsage.ioWaitUsage = fracToShort(ioWaitDiff / totalTicks);
				}
			} else {
				cpuUsage.ioWaitUsage = iowait; // Assign error code.
			}
			if (steal >= 0) {
				totalTicks = totalCalculationTicks + stealDiff;
				if (totalTicks == 0) {
					cpuUsage.stealUsage = 0;
				} else {
					cpuUsage.stealUsage = fracToShort(stealDiff / totalTicks);
				}
			} else {
				cpuUsage.stealUsage = steal; // Assign error code.
			}

			cpuUsage.lastUserTicks   = user;
			cpuUsage.lastNiceTicks   = nice;
			cpuUsage.lastSystemTicks = sys;
			if (iowait >= 0) {
				cpuUsage.lastIoWaitTicks = iowait;
			}
			cpuUsage.lastIdleTicks   = idle;
			if (steal >= 0) {
				cpuUsage.lastStealTicks  = steal;
			}
		}
	#endif

	#ifdef __linux__
		void readNextWordAndAssertEqual(const char **data, const StaticString &expected) const {
			if (readNextWord(data) != expected) {
				throw ParseException();
			}
		}

		void queryMemInfo(SystemMetrics &metrics) const {
			string contents;
			bool hasContents = false;
			try {
				contents = unsafeReadFile("/proc/meminfo");
				hasContents = true;
			} catch (const SystemException &) {
			}
			if (hasContents) {
				try {
					parseMemInfo(metrics, contents);
				} catch (const ParseException &) {
					throw RuntimeException("Cannot parse information in /proc/meminfo");
				}
			} else {
				metrics.ramTotal = metrics.ramUsed = -1;
				metrics.swapTotal = metrics.swapUsed = -1;
			}
		}

		void parseMemInfo(SystemMetrics &metrics, const string &data) const {
			const char *start = data.c_str();
			long long memTotal = -1, memFree = -1, buffers = -1, cached = -1;
			long long swapTotal = -1, swapFree = -1;

			while (start != NULL) {
				StaticString name = readNextWord(&start);
				long long value = readNextWordAsLongLong(&start);
				if (!skipToNextLine(&start) || *start == '\0') {
					start = NULL;
				}

				if (name == "MemTotal:") {
					memTotal = value;
				} else if (name == "MemFree:") {
					memFree = value;
				} else if (name == "Buffers:") {
					buffers = value;
				} else if (name == "Cached:") {
					cached = value;
				} else if (name == "SwapTotal:") {
					swapTotal = value;
				} else if (name == "SwapFree:") {
					swapFree = value;
				}
			}

			if (memTotal != -1) {
				metrics.ramTotal = memTotal;
				if (memFree != -1) {
					metrics.ramUsed = memTotal - memFree;
					if (buffers != -1) {
						metrics.ramUsed -= buffers;
					}
					if (cached != -1) {
						metrics.ramUsed -= cached;
					}
				} else {
					metrics.ramUsed = -1;
				}
			} else {
				metrics.ramTotal = metrics.ramUsed = -1;
			}
			if (swapTotal != -1) {
				metrics.swapTotal = swapTotal;
				if (swapFree != -1) {
					metrics.swapUsed = swapTotal - swapFree;
				} else {
					metrics.swapUsed = -1;
				}
			} else {
				metrics.swapTotal = metrics.swapUsed = -1;
			}
		}

		void queryProcStat(SystemMetrics &metrics) const {
			string contents;
			bool hasContents = false;
			try {
				contents = unsafeReadFile("/proc/stat");
				hasContents = true;
			} catch (const SystemException &) {
			}
			if (hasContents) {
				try {
					parseProcStat(metrics, contents);
				} catch (const ParseException &) {
					throw RuntimeException("Cannot parse information in /proc/stat");
				}
			} else {
				failReadingCpuUsages(metrics);
			}
		}

		void parseProcStat(SystemMetrics &metrics, const string &data) const {
			const char *start = data.c_str();
			unsigned long long forkCount = 0;

			while (start != NULL) {
				if (*start == '\n') {
					// Empty line. Skip to next line.
					start++;
					continue;
				}

				StaticString name = readNextWord(&start);

				if (name.size() > 3 && startsWith(name, "cpu")) {
					const char *numStart = name.data() + 3;
					unsigned long num = strtoul(numStart, NULL, 10);

					long long user = readNextWordAsLongLong(&start);
					long long nice = readNextWordAsLongLong(&start);
					long long sys  = readNextWordAsLongLong(&start);
					long long idle = readNextWordAsLongLong(&start);
					long long iowait = readNextWordAsLongLong(&start);
					readNextWordAsLongLong(&start); // irq
					readNextWordAsLongLong(&start); // softirq
					long long steal;
					try {
						steal = readNextWordAsLongLong(&start);
					} catch (const ParseException &) {
						// Not supported on Linux < 2.6.11
						steal = -2;
					}

					if (num + 1 > metrics.cpuUsages.size()) {
						metrics.cpuUsages.resize(num + 1);
					}
					updateCpuMetrics(
						metrics.cpuUsages[num],
						user,
						nice,
						sys,
						iowait,
						idle,
						steal);
				} else if (name == "processes") {
					forkCount = (long long) readNextWordAsLongLong(&start);
				}

				if (!skipToNextLine(&start) || *start == '\0') {
					start = NULL;
				}
			}

			if (forkCount == 0) {
				metrics.forkRate = -1;
			} else {
				metrics.forkRateSpeedMeter.addSample(forkCount);
				metrics.forkRate = metrics.forkRateSpeedMeter.currentSpeed();
			}
		}

		void queryProcVmstat(SystemMetrics &metrics) const {
			string contents;
			bool hasContents = false;
			try {
				contents = unsafeReadFile("/proc/vmstat");
				hasContents = true;
			} catch (const SystemException &) {
			}
			if (hasContents) {
				try {
					parseProcVmstat(metrics, contents);
				} catch (const ParseException &) {
					throw RuntimeException("Cannot parse information in /proc/vmstat");
				}
			} else {
				failReadingCpuUsages(metrics);
			}
		}

		void parseProcVmstat(SystemMetrics &metrics, const string &data) const {
			const char *start = data.c_str();
			long long pswpin = -1, pswpout = -1;

			while (start != NULL) {
				StaticString name = readNextWord(&start);
				long long value = readNextWordAsLongLong(&start);

				if (!skipToNextLine(&start) || *start == '\0') {
					start = NULL;
				}

				if (name == "pswpin") {
					pswpin = value;
				} else if (name == "pswpout") {
					pswpout = value;
				}
			}

			if (pswpin == -1 || pswpout == -1) {
				metrics.swapInRate = -1;
				metrics.swapOutRate = -1;
			} else {
				metrics.swapInSpeedMeter.addSample(pswpin * pageSize / 1024);
				metrics.swapOutSpeedMeter.addSample(pswpout * pageSize / 1024);
				metrics.swapInRate  = metrics.swapInSpeedMeter.currentSpeed();
				metrics.swapOutRate = metrics.swapOutSpeedMeter.currentSpeed();
			}
		}

		void queryBoottimeFromSysinfo(SystemMetrics &metrics) const {
			if (metrics.boottime == -1) {
				struct sysinfo info;

				if (sysinfo(&info) != -1) {
					metrics.boottime = (long long) SystemTime::get() - (long long) info.uptime;
				}
			}
		}
	#endif

	#ifdef __APPLE__
		mach_port_t hostPort;

		void collectOSX(SystemMetrics &metrics) const {
			kern_return_t status;
			mach_msg_type_number_t count;
			host_basic_info_data_t hostInfo;
			vm_statistics64_data_t vmStat;
			struct xsw_usage swap;
			size_t bufSize = sizeof(swap);
			int mib[2] = { CTL_VM, VM_SWAPUSAGE };
			unsigned int cpuCount;
			processor_cpu_load_info_t cpuLoads;

			// Query total RAM.
			count = HOST_BASIC_INFO_COUNT;
			status = host_info(hostPort, HOST_BASIC_INFO, (host_info_t) &hostInfo,
				&count);
			if (status == KERN_SUCCESS) {
				metrics.ramTotal = hostInfo.max_mem / 1024;
			} else {
				metrics.ramTotal = -1;
			}

			// Query system memory usage.
			// We regard memory usage as the sum of active, wired and compressed memory.
			// Active + wired is shown as "App memory" in Activity Monitor.
			count = HOST_VM_INFO64_COUNT;
			status = host_statistics64(hostPort, HOST_VM_INFO64, (host_info64_t) &vmStat,
				&count);
			if (status == KERN_SUCCESS) {
				metrics.ramUsed = ((ssize_t) vmStat.active_count + vmStat.wire_count);
				#if __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_9
					metrics.ramUsed += vmStat.compressor_page_count;
				#endif
				metrics.ramUsed = metrics.ramUsed * (pageSize / 1024);
			} else {
				metrics.ramUsed = -1;
			}

			// Query swap.
			if (sysctl(mib, 2, &swap, &bufSize, NULL, 0) == 0) {
				metrics.swapTotal = swap.xsu_total / 1024;
				metrics.swapUsed = swap.xsu_used / 1024;
			} else {
				metrics.swapTotal = metrics.swapUsed = -1;
			}

			// Query CPU usages.
			status = host_processor_info(hostPort, PROCESSOR_CPU_LOAD_INFO,
            	&cpuCount, (processor_info_array_t *) &cpuLoads, &count);
			if (status == KERN_SUCCESS) {
				if ((unsigned int) metrics.cpuUsages.size() != cpuCount) {
					metrics.cpuUsages.resize(cpuCount);
				}
				for (unsigned int i = 0; i < cpuCount; i++) {
					unsigned int *cpuTicks = cpuLoads[i].cpu_ticks;
					updateCpuMetrics(
						metrics.cpuUsages[i],
						cpuTicks[CPU_STATE_USER],
						cpuTicks[CPU_STATE_NICE],
						cpuTicks[CPU_STATE_SYSTEM],
						-2, /* OS X does not support iowait */
						cpuTicks[CPU_STATE_IDLE],
						-2  /* OS X does not support steal */);
				}
			} else {
				failReadingCpuUsages(metrics);
			}
		}
	#endif

	#ifdef __FreeBSD__
		int kern_smp_maxcpus[3];
		int kern_cp_times[2];
		int vm_active_count[4];
		int vm_wire_count[4];

		template<typename IntegerType>
		bool querySysctl(int mib1, int mib2, IntegerType &result) const {
			int mib[2] = { mib1, mib2 };
			IntegerType val;
			size_t len = sizeof(IntegerType);
			if (sysctl(mib, 2, &val, &len, NULL, 0) == 0) {
				if (len == sizeof(IntegerType)) {
					result = val;
					return true;
				} else {
					return false;
				}
			} else {
				return false;
			}
		}

		template<typename IntegerType>
		bool querySysctlMib(int mib[], size_t mibsize, IntegerType &result) const {
			IntegerType val;
			size_t len = sizeof(IntegerType);
			if (mib[0] == -1) {
				return false;
			} else if (sysctl(mib, mibsize / sizeof(int), &val, &len, NULL, 0) == 0) {
				if (len == sizeof(IntegerType)) {
					result = val;
					return true;
				} else {
					return false;
				}
			} else {
				return false;
			}
		}

		void collectFreeBSD(SystemMetrics &metrics) const {
			size_t len;
			size_t val_size_t;
			unsigned int val1_uint, val2_uint;
			int vm_active_count[sizeof(this->vm_active_count) / sizeof(int)];
			int vm_wire_count[sizeof(this->vm_wire_count) / sizeof(int)];

			// Query active CPU count.
			if (!queryCpuUsage(metrics)) {
				failReadingCpuUsages(metrics);
			}

			// Query memory.
			memcpy(vm_active_count, this->vm_active_count, sizeof(vm_active_count));
			memcpy(vm_wire_count, this->vm_wire_count, sizeof(vm_wire_count));
			if (metrics.ramTotal < 0) {
				if (querySysctl<size_t>(CTL_HW, HW_PHYSMEM, val_size_t)) {
					metrics.ramTotal = val_size_t / 1024;
				} else {
					metrics.ramTotal = -1;
				}
			}
			if (metrics.ramTotal >= 0
			 && querySysctlMib<unsigned int>(vm_active_count, sizeof(vm_active_count), val1_uint)
			 && querySysctlMib<unsigned int>(vm_wire_count, sizeof(vm_wire_count), val2_uint))
			{
				metrics.ramUsed = ((long long) val1_uint + val2_uint) * pageSize / 1024;
			} else {
				metrics.ramUsed  = -1;
			}

			// Query swap.
			int mib[17];
			size_t mibsize = 16;
			if (sysctlnametomib("vm.swap_info", mib, &mibsize) == 0) {
				long long total = 0, used = 0;

				for (int n = 0; ; n++) {
					struct xswdev xsw;

					mib[mibsize] = n;
					len = sizeof(xsw);
					if (sysctl(mib, mibsize + 1, &xsw, &len, NULL, 0) == -1) {
						break;
					}
					if (xsw.xsw_version != XSWDEV_VERSION) {
						metrics.swapTotal = -1;
						metrics.swapUsed  = -1;
						break;
					}

					total += (long long) xsw.xsw_nblks * pageSize;
					used  += (long long) xsw.xsw_used  * pageSize;
				}

				metrics.swapTotal = total / 1024;
				metrics.swapUsed  = used / 1024;
			} else {
				metrics.swapTotal = -1;
				metrics.swapUsed  = -1;
			}
		}

		bool cpuStatesAreEmpty(const long *states) const {
			for (int i = 0; i < CPUSTATES; i++) {
				if (states[i] != 0) {
					return false;
				}
			}
			return true;
		}

		bool queryCpuUsage(SystemMetrics &metrics) const {
			int kern_smp_maxcpus[sizeof(this->kern_smp_maxcpus) / sizeof(int)];
			int kern_cp_times[sizeof(this->kern_cp_times) / sizeof(int)];
			long *times = NULL;
			int maxcpus;
			size_t size;
			unsigned int i, j;

			// Preparation.
			memcpy(kern_smp_maxcpus, this->kern_smp_maxcpus, sizeof(kern_smp_maxcpus));
			memcpy(kern_cp_times, this->kern_cp_times, sizeof(kern_cp_times));
			if (kern_smp_maxcpus[0] == -1 || kern_cp_times[0] == -1) {
				goto error;
			}

			// Query maximum number of supported CPUs.
			if (!querySysctlMib<int>(kern_smp_maxcpus, sizeof(kern_smp_maxcpus),
				maxcpus))
			{
				goto error;
			}

			// Query CPU times.
			size = sizeof(long) * maxcpus * CPUSTATES;
			times = (long *) malloc(size);
			if (times == NULL) {
				goto error;
			}
			if (sysctl(kern_cp_times, sizeof(kern_cp_times) / sizeof(int),
				times, &size, NULL, 0) == -1)
			{
				goto error;
			}

			i = j = 0;
			while (i < size / CPUSTATES / sizeof(long)) {
				if (!cpuStatesAreEmpty(&times[i * CPUSTATES])) {
					if (metrics.cpuUsages.size() < j + 1) {
						metrics.cpuUsages.resize(j + 1);
					}
					updateCpuMetrics(
						metrics.cpuUsages[j],
						times[i * CPUSTATES + CP_USER],
						times[i * CPUSTATES + CP_NICE],
						times[i * CPUSTATES + CP_SYS],
						-2, /* FreeBSD does not support iowait */
						times[i * CPUSTATES + CP_IDLE],
						-2 /* FreeBSD does not support steal */);
					j++;
				}
				i++;
			}

			if (metrics.cpuUsages.size() != j) {
				metrics.cpuUsages.resize(j);
			}

			return true;

			error:
			if (times != NULL) {
				free(times);
			}
			return false;
		}
	#endif

	#if defined(__APPLE__) || defined(__FreeBSD__)
		void queryBoottimeFromSysctl(SystemMetrics &metrics) const {
			if (metrics.boottime == -1) {
				struct timeval boottime;
				size_t len = sizeof(boottime);
				int mib[2] = { CTL_KERN, KERN_BOOTTIME };

				if (sysctl(mib, 2, &boottime, &len, NULL, 0) == 0) {
					metrics.boottime = boottime.tv_sec;
				} else {
					metrics.boottime = -1;
				}
			}
		}
	#endif

	void queryOsRelease(SystemMetrics &metrics) const {
		struct utsname name;

		if (metrics.kernelVersion.empty() && uname(&name) == 0) {
			metrics.kernelVersion = name.release;
		}
	}

public:
	SystemMetricsCollector() {
		#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
			pageSize = getpagesize();
		#endif
		#if defined(__APPLE__)
			hostPort = mach_host_self();
		#endif
		#if defined(__FreeBSD__)
			size_t len;

			len = sizeof(kern_smp_maxcpus) / sizeof(int);
			if (sysctlnametomib("kern.smp.maxcpus", kern_smp_maxcpus, &len) == -1) {
				kern_smp_maxcpus[0] = -1;
			}

			len = sizeof(kern_cp_times) / sizeof(int);
			if (sysctlnametomib("kern.cp_times", kern_cp_times, &len) == -1) {
				kern_cp_times[0] = -1;
			}

			len = sizeof(vm_active_count) / sizeof(int);
			if (sysctlnametomib("vm.stats.vm.v_active_count", vm_active_count, &len) == -1) {
				vm_active_count[0] = -1;
			}

			len = sizeof(vm_wire_count) / sizeof(int);
			if (sysctlnametomib("vm.stats.vm.v_wire_count", vm_wire_count, &len) == -1) {
				vm_wire_count[0] = -1;
			}
		#endif
	}

	/**
	 * If some information cannot be queried, then this method does not
	 * throw an exception. Instead, that particular metric in the metrics
	 * object is just not updated. However if something really unexpected
	 * goes wrong (such as when a command did not return the output it's
	 * supposed to return, so that we're unable to parse the output) then
	 * a RuntimeException is thrown.
	 *
	 * @throws RuntimeException
	 */
	void collect(SystemMetrics &metrics) const {
		#if defined(__linux__)
			queryMemInfo(metrics);
			queryProcStat(metrics);
			queryProcVmstat(metrics);
			queryBoottimeFromSysinfo(metrics);
			queryLoadAvg(metrics);
		#elif defined(__APPLE__)
			collectOSX(metrics);
			queryBoottimeFromSysctl(metrics);
			queryLoadAvg(metrics);
		#elif defined(__FreeBSD__)
			collectFreeBSD(metrics);
			queryBoottimeFromSysctl(metrics);
			queryLoadAvg(metrics);
		#endif
		queryOsRelease(metrics);
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_METRICS_COLLECTOR_H_ */

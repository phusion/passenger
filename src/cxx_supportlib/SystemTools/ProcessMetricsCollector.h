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
#ifndef _PASSENGER_SYSTEM_TOOLS_PROCESS_METRICS_COLLECTOR_H_
#define _PASSENGER_SYSTEM_TOOLS_PROCESS_METRICS_COLLECTOR_H_

#include <boost/cstdint.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <oxt/system_calls.hpp>
#include <string>
#include <vector>
#include <map>

#ifdef __APPLE__
	#include <mach/mach_traps.h>
	#include <mach/mach_init.h>
	#include <mach/mach_vm.h>
	#include <mach/mach_port.h>
#endif
#if !defined(__NetBSD__) && !defined(__OpenBSD__)
	// NetBSD does not support -p with multiple PIDs.
	// https://code.google.com/p/phusion-passenger/issues/detail?id=736
	// OpenBSD 5.2 doesn't support it either
	#define PS_SUPPORTS_MULTIPLE_PIDS
	#include <set>
#endif

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>

#include <StaticString.h>
#include <Exceptions.h>
#include <ProcessManagement/Spawn.h>
#include <FileTools/FileManip.h>
#include <Utils/ScopeGuard.h>
#include <IOTools/IOUtils.h>
#include <StrIntTools/StringScanning.h>

namespace Passenger {

using namespace boost;
using namespace std;
using namespace oxt;

/** All sizes are in KB. */
struct ProcessMetrics {
	pid_t   pid;
	pid_t   ppid;
	boost::uint8_t cpu;
	/** Resident Set Size, amount of memory in RAM. Does not include swap.
	 * -1 if not yet known, 0 if completely swapped out.
	 */
	ssize_t  rss;
	/** Proportional Set Size, see measureRealMemory(). Does not include swap.
	 * -1 if unknown, 0 if completely swapped out.
	 */
	ssize_t  pss;
	/** Private dirty RSS, see measureRealMemory(). Does not include swap.
	 * -1 if unknown, 0 if completely swapped out.
	 */
	ssize_t  privateDirty;
	/** Amount of memory in swap.
	 * -1 if unknown, 0 if no swap used.
	 */
	ssize_t  swap;
	/** OS X Snow Leopard does not report the VM size correctly, so don't use this. */
	ssize_t  vmsize;
	pid_t   processGroupId;
	uid_t   uid;
	string  command;

	ProcessMetrics() {
		pid = (pid_t) -1;
		ppid = (pid_t) -1;
		cpu = -1;
		rss = -1;
		pss = -1;
		privateDirty = -1;
		swap = -1;
		vmsize = -1;
		processGroupId = (pid_t) -1;
		uid = (uid_t) -1;
	}

	bool isValid() const {
		return pid != (pid_t) -1;
	}

	/**
	 * Returns an estimate of the "real" memory usage of a process in KB.
	 * We don't use the PSS here because that would mean if another
	 * process that shares memory quits, this process's memory usage
	 * would suddenly go up.
	 */
	size_t realMemory() const {
		ssize_t swap;
		if (this->swap != -1) {
			swap = this->swap;
		} else {
			swap = 0;
		}
		if (privateDirty != -1) {
			return privateDirty + swap;
		} else if (rss != -1) {
			return rss + swap;
		} else {
			return 0;
		}
	}
};

class ProcessMetricMap: public map<pid_t, ProcessMetrics> {
public:
	/**
	 * Returns the total memory usage of all processes in KB, possibly
	 * including shared memory.
	 * If measurable, the return value only includes the processes' private
	 * memory usage (swap is accounted for), and <em>shared</em> is set to the
	 * amount of shared memory.
	 * If not measurable, then the return value is an estimate of the total
	 * memory usage of all processes (which may or may not include shared memory
	 * as well), and <em>shared</em> is set to -1.
	 */
	size_t totalMemory(ssize_t &shared) const {
		const_iterator it, end = this->end();
		bool pssAndPrivateDirtyAvailable = true;

		for (it = begin(); it != end && pssAndPrivateDirtyAvailable; it++) {
			const ProcessMetrics &metric = it->second;
			pssAndPrivateDirtyAvailable = pssAndPrivateDirtyAvailable &&
				metric.pss != -1 && metric.privateDirty != -1;
		}

		if (pssAndPrivateDirtyAvailable) {
			size_t total = 0;
			size_t priv = 0;

			for (it = begin(); it != end; it++) {
				const ProcessMetrics &metric = it->second;
				total += metric.pss;
				priv += metric.privateDirty;
			}

			shared = total - priv;
			return total;
		} else {
			size_t total = 0;

			for (it = begin(); it != end; it++) {
				const ProcessMetrics &metric = it->second;
				total += metric.realMemory();
			}

			shared = -1;
			return total;
		}
	}
};

/**
 * Utility class for collection metrics on processes, such as CPU usage, memory usage,
 * command name, etc.
 */
class ProcessMetricsCollector {
private:
	bool canMeasureRealMemory;
	string psOutput;

	template<typename Collection, typename ConstIterator>
	ProcessMetricMap parsePsOutput(const string &output, const Collection &allowedPids) const {
		ProcessMetricMap result;
		const char *start = output.c_str();

		// Ignore first line, it contains the column names.
		if (!skipToNextLine(&start) || *start == '\0') {
			start = NULL;
		}

		#ifndef PS_SUPPORTS_MULTIPLE_PIDS
			set<pid_t> pids;
			ConstIterator it, end = allowedPids.end();
			for (it = allowedPids.begin(); it != allowedPids.end(); it++) {
				pids.insert(*it);
			}
		#endif

		// Parse each line.
		while (start != NULL) {
			ProcessMetrics metrics;

			metrics.pid  = (pid_t) readNextWordAsLongLong(&start);
			metrics.ppid = (pid_t) readNextWordAsLongLong(&start);
			metrics.cpu  = readNextWordAsInt(&start);
			metrics.rss  = (size_t) readNextWordAsLongLong(&start);
			metrics.vmsize  = (size_t) readNextWordAsLongLong(&start);
			metrics.processGroupId = (pid_t) readNextWordAsLongLong(&start);
			metrics.uid  = (uid_t) readNextWordAsLongLong(&start);
			metrics.command = readRestOfLine(start);

			bool pidAllowed;
			#ifdef PS_SUPPORTS_MULTIPLE_PIDS
				pidAllowed = true;
			#else
				pidAllowed = pids.find(metrics.pid) != pids.end();
			#endif

			if (pidAllowed) {
				result[metrics.pid] = metrics;

				start = strchr(start, '\n');
				if (start != NULL) {
					// Skip to beginning of next line.
					start++;
					if (*start == '\0') {
						start = NULL;
					}
				}
			}
		}
		return result;
	}

	static void afterFork() {
		// Make ps nicer, we want to have as little impact on the rest
		// of the system as possible while collecting the metrics.
		int prio = getpriority(PRIO_PROCESS, getpid());
		prio++;
		if (prio > 20) {
			prio = 20;
		}
		setpriority(PRIO_PROCESS, getpid(), prio);
	}

public:
	ProcessMetricsCollector() {
		#ifdef __APPLE__
			canMeasureRealMemory = true;
		#else
			canMeasureRealMemory = fileExists("/proc/self/smaps");
		#endif
	}

	/** Mock 'ps' output, used by unit tests. */
	void setPsOutput(const string &data) {
		this->psOutput = data;
	}

	/**
	 * Collect metrics for the given process IDs. Nonexistant PIDs are not
	 * included in the result.
	 *
	 * Returns a map which maps a given PID to its collected metrics.
	 *
	 * @throws ParseException The ps output cannot be parsed.
	 * @throws SystemException Error collecting the ps output or error querying memory usage.
	 */
	template<typename Collection, typename ConstIterator>
	ProcessMetricMap collect(const Collection &pids) const {
		if (pids.empty()) {
			return ProcessMetricMap();
		}

		ConstIterator it;
		// The list of PIDs must follow -p without a space.
		// https://groups.google.com/forum/#!topic/phusion-passenger/WKXy61nJBMA
		string pidsArg = "-p";

		for (it = pids.begin(); it != pids.end(); it++) {
			pidsArg.append(toString(*it));
			pidsArg.append(",");
		}
		if (pidsArg[pidsArg.size() - 1] == ',') {
			pidsArg.resize(pidsArg.size() - 1);
		}

		// The list of format arguments must also follow -o
		// without a space.
		// https://github.com/phusion/passenger/pull/94
		string fmtArg = "-o";
		#if defined(sun) || defined(__sun)
			fmtArg.append("pid,ppid,pcpu,rss,vsz,pgid,uid,args");
		#else
			fmtArg.append("pid,ppid,%cpu,rss,vsize,pgid,uid,command");
		#endif

		const char *command[] = {
			"ps", fmtArg.c_str(),
			#ifdef PS_SUPPORTS_MULTIPLE_PIDS
				pidsArg.c_str(),
			#endif
			NULL
		};

		SubprocessOutput psOutput;
		psOutput.data = this->psOutput;
		if (psOutput.data.empty()) {
			SubprocessInfo info;
			runCommandAndCaptureOutput(command, info, psOutput,
				1024 * 1024, true, afterFork);
			if (psOutput.data.empty()) {
				throw RuntimeException("The 'ps' command failed");
			}
		}
		pidsArg.resize(0);
		fmtArg.resize(0);
		ProcessMetricMap result = parsePsOutput<Collection, ConstIterator>(
			psOutput.data, pids);
		psOutput.data.resize(0);
		if (canMeasureRealMemory) {
			ProcessMetricMap::iterator it;
			for (it = result.begin(); it != result.end(); it++) {
				ProcessMetrics &metric = it->second;
				measureRealMemory(metric.pid, metric.pss,
					metric.privateDirty, metric.swap);
			}
		}
		return result;
	}

	ProcessMetricMap collect(const vector<pid_t> &pids) const {
		return collect< vector<pid_t>, vector<pid_t>::const_iterator >(pids);
	}

	/**
	 * Attempt to measure various parts of a process's memory usage that may
	 * contribute to insight as to what its "real" memory usage might be.
	 * Collected information are:
	 * - The proportional set size: total size of a process's pages that are in
	 *   memory, where the size of each page is divided by the number of processes
	 *   sharing it.
	 * - The private dirty RSS.
	 * - Amount of memory in swap.
	 *
	 * At this time only OS X and recent Linux versions (>= 2.6.25) support
	 * measuring the proportional set size. Usually root privileges are required.
	 *
	 * pss, privateDirty and swap can each be individually set to -1 if that
	 * part cannot be measured, e.g. because we do not have permission
	 * to do so or because the OS does not support measuring it.
	 */
	static void measureRealMemory(pid_t pid, ssize_t &pss, ssize_t &privateDirty, ssize_t &swap) {
		#ifdef __APPLE__
			kern_return_t ret;
			mach_port_t task;

			swap = -1;

			ret = task_for_pid(mach_task_self(), pid, &task);
			if (ret != KERN_SUCCESS) {
				pss = -1;
				privateDirty = -1;
				return;
			}

			mach_vm_address_t addr = 0;
			int pagesize = getpagesize();

			// In bytes.
			pss = 0;
			privateDirty = 0;

			while (true) {
				mach_vm_address_t size;
				vm_region_top_info_data_t info;
				mach_msg_type_number_t count = VM_REGION_TOP_INFO_COUNT;
				mach_port_t object_name;

				ret = mach_vm_region(task, &addr, &size, VM_REGION_TOP_INFO,
					(vm_region_info_t) &info, &count, &object_name);
				if (ret != KERN_SUCCESS) {
					break;
				}

				if (info.share_mode == SM_PRIVATE) {
					// shared_pages_resident here means that region
					// has shared memory only "shared" between 1 process.
					pss += info.private_pages_resident * pagesize;
					pss += info.shared_pages_resident * pagesize;
					privateDirty += info.private_pages_resident * pagesize;
				} else if (info.share_mode == SM_COW) {
					pss += info.private_pages_resident * pagesize;
					pss += info.shared_pages_resident * pagesize / info.ref_count;
					privateDirty += info.private_pages_resident * pagesize;
				} else if (info.share_mode == SM_SHARED) {
					pss += info.shared_pages_resident * pagesize / info.ref_count;
				}

				addr += size;
			}

			mach_port_deallocate(mach_task_self(), task);

			// Convert result back to KB.
			pss /= 1024;
			privateDirty /= 1024;
		#else
			string smapsFilename = "/proc/";
			smapsFilename.append(toString(pid));
			smapsFilename.append("/smaps");

			FILE *f = syscalls::fopen(smapsFilename.c_str(), "r");
			if (f == NULL) {
				error:
				pss = -1;
				privateDirty = -1;
				swap = -1;
				return;
			}

			StdioGuard guard(f, NULL, 0);
			bool hasPss = false;
			bool hasPrivateDirty = false;
			bool hasSwap = false;

			// In KB.
			pss = 0;
			privateDirty = 0;
			swap = 0;

			while (!feof(f)) {
				char line[1024 * 4];
				const char *buf;

				buf = fgets(line, sizeof(line), f);
				if (buf == NULL) {
					if (ferror(f)) {
						goto error;
					} else {
						break;
					}
				}
				try {
					if (startsWith(line, "Pss:")) {
						/* Linux supports Proportional Set Size since kernel 2.6.25.
						 * See kernel commit ec4dd3eb35759f9fbeb5c1abb01403b2fde64cc9.
						 */
						hasPss = true;
						readNextWord(&buf);
						pss += readNextWordAsLongLong(&buf);
						if (readNextWord(&buf) != "kB") {
							goto error;
						}
					} else if (startsWith(line, "Private_Dirty:")) {
						hasPrivateDirty = true;
						readNextWord(&buf);
						privateDirty += readNextWordAsLongLong(&buf);
						if (readNextWord(&buf) != "kB") {
							goto error;
						}
					} else if (startsWith(line, "Swap:")) {
						hasSwap = true;
						readNextWord(&buf);
						swap += readNextWordAsLongLong(&buf);
						if (readNextWord(&buf) != "kB") {
							goto error;
						}
					}
				} catch (const ParseException &) {
					goto error;
				}
			}

			if (!hasPss) {
				pss = -1;
			}
			if (!hasPrivateDirty) {
				privateDirty = -1;
			}
			if (!hasSwap) {
				swap = -1;
			}
		#endif
	}
};

} // namespace Passenger

#endif /* _PASSENGER_SYSTEM_TOOLS_PROCESS_METRICS_COLLECTOR_H_ */

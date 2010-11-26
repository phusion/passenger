/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_PROCESS_METRICS_COLLECTOR_H_
#define _PASSENGER_PROCESS_METRICS_COLLECTOR_H_

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
#include <Utils.h>
#include <Utils/ScopeGuard.h>

namespace Passenger {

using namespace boost;
using namespace std;
using namespace oxt;

/** All sizes are in KB. */
struct ProcessMetrics {
	pid_t   pid;
	pid_t   ppid;
	uint8_t cpu;
	/** Resident Set Size, amount of memory in RAM. Does not include swap.
	 * 0 if completely swapped out.
	 */
	size_t  rss;
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
	size_t  vmsize;
	pid_t   processGroupId;
	string  command;
	
	ProcessMetrics() {
		pid = (pid_t) -1;
		pss = -1;
		privateDirty = -1;
		swap = -1;
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
		} else {
			return rss + swap;
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
public:
	struct ParseException {};
	
private:
	bool canMeasureRealMemory;
	string psOutput;
	
	/**
	 * Scan the given data for the first word that appears on the first line.
	 * Leading whitespaces (but not newlines) are ignored. If a word is found
	 * then the word is returned and the data pointer is moved to the end of
	 * the word. Otherwise, a ParseException is thrown.
	 *
	 * @post result.size() > 0
	 */
	static StaticString readNextWord(const char **data) {
		// Skip leading whitespaces.
		while (**data == ' ') {
			(*data)++;
		}
		if (**data == '\n' || **data == '\0') {
			throw ParseException();
		}
		
		// Find end of word and extract the word.
		const char *endOfWord = *data;
		while (*endOfWord != ' ' && *endOfWord != '\n' && *endOfWord != '\0') {
			endOfWord++;
		}
		StaticString result(*data, endOfWord - *data);
		
		// Move data pointer to the end of this word.
		*data = endOfWord;
		return result;
	}
	
	static long long readNextWordAsLongLong(const char **data) {
		StaticString word = readNextWord(data);
		char nullTerminatedWord[word.size() + 1];
		memcpy(nullTerminatedWord, word.c_str(), word.size());
		nullTerminatedWord[word.size()] = '\0';
		if (*nullTerminatedWord == '\0') {
			throw ParseException();
		} else {
			return atoll(nullTerminatedWord);
		}
	}
	
	static int readNextWordAsInt(const char **data) {
		StaticString word = readNextWord(data);
		char nullTerminatedWord[word.size() + 1];
		memcpy(nullTerminatedWord, word.c_str(), word.size());
		nullTerminatedWord[word.size()] = '\0';
		if (*nullTerminatedWord == '\0') {
			throw ParseException();
		} else {
			return atoi(nullTerminatedWord);
		}
	}
	
	string runCommandAndCaptureOutput(const char **command) const {
		pid_t pid;
		int e, p[2];
		
		syscalls::pipe(p);
		
		this_thread::disable_syscall_interruption dsi;
		pid = syscalls::fork();
		if (pid == 0) {
			// Make ps nicer, we want to have as little impact on the rest
			// of the system as possible while collecting the metrics.
			int prio = getpriority(PRIO_PROCESS, getpid());
			prio++;
			if (prio > 20) {
				prio = 20;
			}
			setpriority(PRIO_PROCESS, getpid(), prio);
			
			dup2(p[1], 1);
			close(p[0]);
			close(p[1]);
			execvp(command[0], (char * const *) command);
			_exit(1);
		} else if (pid == -1) {
			e = errno;
			syscalls::close(p[0]);
			syscalls::close(p[1]);
			throw SystemException("Cannot fork() a new process", e);
		} else {
			bool done = false;
			string result;
			
			syscalls::close(p[1]);
			while (!done) {
				char buf[1024 * 4];
				ssize_t ret;
				
				try {
					this_thread::restore_syscall_interruption rsi(dsi);
					ret = syscalls::read(p[0], buf, sizeof(buf));
				} catch (const thread_interrupted &) {
					syscalls::close(p[0]);
					syscalls::kill(SIGKILL, pid);
					syscalls::waitpid(pid, NULL, 0);
					throw;
				}
				if (ret == -1) {
					e = errno;
					syscalls::close(p[0]);
					syscalls::kill(SIGKILL, pid);
					syscalls::waitpid(pid, NULL, 0);
					throw SystemException("Cannot read output from the 'ps' command", e);
				}
				done = ret == 0;
				result.append(buf, ret);
			}
			syscalls::close(p[0]);
			syscalls::waitpid(pid, NULL, 0);
			
			if (result.empty()) {
				throw RuntimeException("The 'ps' command failed");
			} else {
				return result;
			}
		}
	}
	
	string readRestOfLine(const char *data) const {
		// Skip leading whitespaces.
		while (*data == ' ') {
			data++;
		}
		// Rest of line is allowed to be empty.
		if (*data == '\n' || *data == '\0') {
			return "";
		}
		
		// Look for newline character. From there, scan back until we've
		// found a non-whitespace character.
		const char *endOfLine = strchr(data, '\n');
		if (endOfLine == NULL) {
			throw ParseException();
		}
		while (*(endOfLine - 1) == ' ') {
			endOfLine--;
		}
		
		return string(data, endOfLine - data);
	}
	
	ProcessMetricMap parsePsOutput(const string &output) const {
		ProcessMetricMap result;
		// Ignore first line, it contains the column names.
		const char *start = strchr(output.c_str(), '\n');
		if (start != NULL) {
			// Skip to beginning of next line.
			start++;
			if (*start == '\0') {
				start = NULL;
			}
		}
		
		// Parse each line.
		while (start != NULL) {
			ProcessMetrics metrics;
			
			metrics.pid  = (pid_t) readNextWordAsLongLong(&start);
			metrics.ppid = (pid_t) readNextWordAsLongLong(&start);
			metrics.cpu  = readNextWordAsInt(&start);
			metrics.rss  = (size_t) readNextWordAsLongLong(&start);
			metrics.vmsize  = (size_t) readNextWordAsLongLong(&start);
			metrics.processGroupId = (pid_t) readNextWordAsLongLong(&start);
			metrics.command = readRestOfLine(start);
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
		return result;
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
	 * @throws ProcessMetricsCollector::ParseException
	 * @throws SystemException
	 * @throws RuntimeException
	 */
	template<typename Collection, typename ConstIterator>
	ProcessMetricMap collect(const Collection &pids) const {
		if (pids.empty()) {
			return ProcessMetricMap();
		}
		
		ConstIterator it;
		string pidsArg;
		
		for (it = pids.begin(); it != pids.end(); it++) {
			pidsArg.append(toString(*it));
			pidsArg.append(",");
		}
		if (!pidsArg.empty() && pidsArg[pidsArg.size() - 1] == ',') {
			pidsArg.resize(pidsArg.size() - 1);
		}
		
		const char *command[] = {
			"ps", "-o",
			#if defined(sun) || defined(__sun)
				"pid,ppid,pcpu,rss,vsz,pgid,args",
			#else
				"pid,ppid,%cpu,rss,vsize,pgid,command",
			#endif
			"-p", pidsArg.c_str(), NULL
		};
		
		string psOutput = this->psOutput;
		if (psOutput.empty()) {
			psOutput = runCommandAndCaptureOutput(command);
		}
		pidsArg.resize(0);
		ProcessMetricMap result = parsePsOutput(psOutput);
		psOutput.resize(0);
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
			
			ScopeGuard fileGuard(boost::bind(syscalls::fclose, f));
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

#endif /* _PASSENGER_PROCESS_METRICS_COLLECTOR_H_ */

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

#include "StaticString.h"
#include "Exceptions.h"
#include "Utils.h"
#include "Utils/FileHandleGuard.h"

namespace Passenger {

using namespace boost;
using namespace std;
using namespace oxt;

/** All sizes are in KB. */
struct ProcessMetrics {
	pid_t   pid;
	pid_t   ppid;
	uint8_t cpu;
	size_t  rss;
	size_t  realMemory;
	/** OS X Snow Leopard does not report the VM size correctly, so don't use this. */
	size_t  vmsize;
	pid_t   processGroupId;
	string  command;
	
	ProcessMetrics() {
		pid = (pid_t) -1;
	}
	
	bool isValid() const {
		return pid != (pid_t) -1;
	}
};

/**
 * Utility class for collection metrics on processes, such as CPU usage, memory usage,
 * command name, etc.
 */
class ProcessMetricsCollector {
public:
	struct ParseException {};
	
	typedef map<pid_t, ProcessMetrics> Map;
	
private:
	bool canMeasureRealMemory;
	
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
	
	Map parsePsOutput(const string &output) const {
		Map result;
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
			metrics.realMemory = 0;
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
	Map collect(const Collection &pids) const {
		if (pids.empty()) {
			return Map();
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
		string psOutput = runCommandAndCaptureOutput(command);
		pidsArg.resize(0);
		Map result = parsePsOutput(psOutput);
		psOutput.resize(0);
		if (canMeasureRealMemory) {
			Map::iterator it;
			for (it = result.begin(); it != result.end(); it++) {
				it->second.realMemory = measureRealMemory(it->second.pid);
			}
		}
		return result;
	}
	
	Map collect(const vector<pid_t> &pids) const {
		return collect< vector<pid_t>, vector<pid_t>::const_iterator >(pids);
	}
	
	/**
	 * Attempt to measure a process's "real" memory usage. This is either the
	 * proportional set size (total size of a process's pages that are in
	 * memory, where the size of each page is divided by the number of processes
	 * sharing it) or the private dirty RSS.
	 * 
	 * At this time only OS X and recent Linux versions (>= 2.6.25) support
	 * measuring the proportional set size. Usually root privileges are required.
	 *
	 * Returns 0 if measuring fails, e.g. because we do not have permission
	 * to do so or because the OS does not support it.
	 */
	static size_t measureRealMemory(pid_t pid) {
		#ifdef __APPLE__
			kern_return_t ret;
			mach_port_t task;
			
			ret = task_for_pid(mach_task_self(), pid, &task);
			if (ret != KERN_SUCCESS) {
				return 0;
			}
			
			mach_vm_address_t addr = 0;
			size_t result = 0; // in bytes
			int pagesize = getpagesize();
			
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
					result += info.private_pages_resident * pagesize;
					result += info.shared_pages_resident * pagesize;
				} else if (info.share_mode == SM_COW) {
					result += info.private_pages_resident * pagesize;
					result += info.shared_pages_resident * pagesize / info.ref_count;
				} else if (info.share_mode == SM_SHARED) {
					result += info.shared_pages_resident * pagesize / info.ref_count;
				}
				
				addr += size;
			}
			
			mach_port_deallocate(mach_task_self(), task);
			return result / 1024;
		#else
			string smapsFilename = "/proc/";
			smapsFilename.append(toString(pid));
			smapsFilename.append("/smaps");
			
			FILE *f = syscalls::fopen(smapsFilename.c_str(), "r");
			if (f == NULL) {
				return 0;
			}
			
			FileHandleGuard fileGuard(f);
			size_t privateDirty = 0; // in KB
			size_t pss = 0;          // in KB
			size_t swap = 0;         // in KB
			
			while (!feof(f)) {
				char line[1024 * 4];
				const char *buf;
				
				buf = fgets(line, sizeof(line), f);
				if (buf == NULL) {
					if (ferror(f)) {
						return 0;
					} else {
						break;
					}
				}
				try {
					if (startsWith(line, "Pss:")) {
						/* Linux supports Proportional Set Size since kernel 2.6.25.
						 * See kernel commit ec4dd3eb35759f9fbeb5c1abb01403b2fde64cc9.
						 */
						readNextWord(&buf);
						pss += readNextWordAsLongLong(&buf);
						if (readNextWord(&buf) != "kB") {
							return 0;
						}
					} else if (startsWith(line, "Private_Dirty:")) {
						readNextWord(&buf);
						privateDirty += readNextWordAsLongLong(&buf);
						if (readNextWord(&buf) != "kB") {
							return 0;
						}
					} else if (startsWith(line, "Swap:")) {
						readNextWord(&buf);
						swap += readNextWordAsLongLong(&buf);
						if (readNextWord(&buf) != "kB") {
							return 0;
						}
					}
				} catch (const ParseException &) {
					return 0;
				}
			}
			
			if (pss != 0) {
				return pss + swap;
			} else {
				return privateDirty + swap;
			}
		#endif
	}
};

} // namespace Passenger

#endif /* _PASSENGER_PROCESS_METRICS_COLLECTOR_H_ */

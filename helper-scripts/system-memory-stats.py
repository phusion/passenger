#!/usr/bin/env python
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.
import sys

if sys.version_info[0] >= 3:
    def bytes_to_str(b):
        return b.decode()
else:
    def bytes_to_str(b):
        return b

def linux_memory_stats():
    import os
    os.execlp('free', 'free', '-m')

def osx_memory_stats():
    import subprocess, re, os, resource

    def popen_read(command):
        return bytes_to_str(subprocess.Popen(command, stdout = subprocess.PIPE).communicate()[0])

    # Get process info
    ps = popen_read(['ps', '-caxm', '-orss,comm'])
    vm = popen_read(['vm_stat'])

    # Iterate processes
    process_lines = ps.split('\n')
    sep = re.compile('[\s]+')
    rss_total = 0 # kB
    for row in range(1, len(process_lines)):
        row_text = process_lines[row].strip()
        row_elements = sep.split(row_text)
        try:
            rss = float(row_elements[0]) * 1024
        except:
            rss = 0 # ignore...
        rss_total += rss

    # Process vm_stat
    vm_lines = vm.split('\n')
    sep = re.compile(':[\s]+')
    vm_stats = {}
    for row in range(1, len(vm_lines) - 2):
        row_text = vm_lines[row].strip()
        row_elements = sep.split(row_text)
        vm_stats[(row_elements[0])] = int(row_elements[1].strip('\.')) * resource.getpagesize()

    print('---- Summary ----')
    print('Wired Memory:\t\t%.1f MB' % (vm_stats["Pages wired down"] / 1024 / 1024))
    print('Active Memory:\t\t%.1f MB' % (vm_stats["Pages active"] / 1024 / 1024))
    print('Inactive Memory:\t%.1f MB' % (vm_stats["Pages inactive"] / 1024 / 1024))
    print('Free Memory:\t\t%.1f MB' % (vm_stats["Pages free"] / 1024 / 1024))
    print('Real Mem Total (ps):\t%.1f MB' % (rss_total / 1024 / 1024))
    print('')
    print('---- vm_stat ----')
    print(vm.strip())

def freebsd_memory_stats():
    # Ported from http://www.cyberciti.biz/files/scripts/freebsd-memory.pl.txt
    ##  freebsd-memory -- List Total System Memory Usage
    ##  Copyright (c) 2003-2004 Ralf S. Engelschall <rse@engelschall.com>
    ##  
    ##  Redistribution and use in source and binary forms, with or without
    ##  modification, are permitted provided that the following conditions
    ##  are met:
    ##  1. Redistributions of source code must retain the above copyright
    ##     notice, this list of conditions and the following disclaimer.
    ##  2. Redistributions in binary form must reproduce the above copyright
    ##     notice, this list of conditions and the following disclaimer in the
    ##     documentation and/or other materials provided with the distribution.
    ##  
    ##  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
    ##  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    ##  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ##  ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
    ##  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    ##  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    ##  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    ##  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    ##  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    ##  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    ##  SUCH DAMAGE.
    import subprocess, re, os, resource

    def popen_read(command):
        return bytes_to_str(subprocess.Popen(command, stdout = subprocess.PIPE).communicate()[0])

    def printf(format, *args):
        print(format % args)

    #   round the physical memory size to the next power of two which is
    #   reasonable for memory cards. We do this by first determining the
    #   guessed memory card size under the assumption that usual computer
    #   hardware has an average of a maximally eight memory cards installed
    #   and those are usually of equal size.
    def mem_rounded(mem_size):
        chip_size  = 1;
        chip_guess = (mem_size / 8) - 1
        while chip_guess != 0:
            chip_guess >>= 1
            chip_size  <<= 1
        mem_round = (mem_size / chip_size + 1) * chip_size
        return mem_round

    sysctl_output = popen_read(['/sbin/sysctl', '-a'])
    sysctl = {}
    regex = re.compile('^([^:]+):\s+(.+)\s*$', re.S | re.M)
    for line in sysctl_output.split('\n'):
        matches = regex.match(line)
        if matches is not None:
            sysctl[matches.group(1)] = matches.group(2)

    #   determine the individual known information
    #   NOTICE: forget hw.usermem, it is just (hw.physmem - vm.stats.vm.v_wire_count).
    #   NOTICE: forget vm.stats.misc.zero_page_count, it is just the subset of
    #           vm.stats.vm.v_free_count which is already pre-zeroed.
    pagesize     = int(sysctl["hw.pagesize"])
    mem_hw       = mem_rounded(int(sysctl["hw.physmem"]))
    mem_phys     = int(sysctl["hw.physmem"])
    mem_all      = int(sysctl["vm.stats.vm.v_page_count"])     * pagesize
    mem_wire     = int(sysctl["vm.stats.vm.v_wire_count"])     * pagesize
    mem_active   = int(sysctl["vm.stats.vm.v_active_count"])   * pagesize
    mem_inactive = int(sysctl["vm.stats.vm.v_inactive_count"]) * pagesize
    mem_cache    = int(sysctl["vm.stats.vm.v_cache_count"])    * pagesize
    mem_free     = int(sysctl["vm.stats.vm.v_free_count"])     * pagesize

    #   determine the individual unknown information
    mem_gap_vm  = mem_all - (mem_wire + mem_active + mem_inactive + mem_cache + mem_free)
    mem_gap_sys = mem_phys - mem_all
    mem_gap_hw  = mem_hw   - mem_phys

    #   determine logical summary information
    mem_total = mem_hw
    mem_avail = mem_inactive + mem_cache + mem_free
    mem_used  = mem_total - mem_avail

    #   information annotations
    info = {
        "mem_wire"     : 'Wired: disabled for paging out',
        "mem_active"   : 'Active: recently referenced',
        "mem_inactive" : 'Inactive: recently not referenced',
        "mem_cache"    : 'Cached: almost avail. for allocation',
        "mem_free"     : 'Free: fully available for allocation',
        "mem_gap_vm"   : 'Memory gap: UNKNOWN',
        "mem_all"      : 'Total real memory managed',
        "mem_gap_sys"  : 'Memory gap: Kernel?!',
        "mem_phys"     : 'Total real memory available',
        "mem_gap_hw"   : 'Memory gap: Segment Mappings?!',
        "mem_hw"       : 'Total real memory installed',
        "mem_used"     : 'Logically used memory',
        "mem_avail"    : 'Logically available memory',
        "mem_total"    : 'Logically total memory',
    }

    #   print system results
    printf("SYSTEM MEMORY INFORMATION:")
    printf("mem_wire:      %7d MB [%3d%%] %s", mem_wire     / (1024*1024), (float(mem_wire)     / mem_all) * 100, info["mem_wire"])
    printf("mem_active:  + %7d MB [%3d%%] %s", mem_active   / (1024*1024), (float(mem_active)   / mem_all) * 100, info["mem_active"])
    printf("mem_inactive:+ %7d MB [%3d%%] %s", mem_inactive / (1024*1024), (float(mem_inactive) / mem_all) * 100, info["mem_inactive"])
    printf("mem_cache:   + %7d MB [%3d%%] %s", mem_cache    / (1024*1024), (float(mem_cache)    / mem_all) * 100, info["mem_cache"])
    printf("mem_free:    + %7d MB [%3d%%] %s", mem_free     / (1024*1024), (float(mem_free)     / mem_all) * 100, info["mem_free"])
    printf("mem_gap_vm:  + %7d MB [%3d%%] %s", mem_gap_vm   / (1024*1024), (float(mem_gap_vm)   / mem_all) * 100, info["mem_gap_vm"])
    printf("-------------- ---------- ------")
    printf("mem_all:     = %7d MB [100%%] %s", mem_all      / (1024*1024), info["mem_all"])
    printf("mem_gap_sys: + %7d MB        %s",  mem_gap_sys  / (1024*1024), info["mem_gap_sys"])
    printf("-------------- ----------")
    printf("mem_phys:    = %7d MB        %s",  mem_phys     / (1024*1024), info["mem_phys"])
    printf("mem_gap_hw:  + %7d MB        %s",  mem_gap_hw   / (1024*1024), info["mem_gap_hw"])
    printf("-------------- ----------")
    printf("mem_hw:      = %7d MB        %s",  mem_hw       / (1024*1024), info["mem_hw"])

    #   print logical results
    print("")
    printf("SYSTEM MEMORY SUMMARY:");
    printf("mem_used:      %7d MB [%3d%%] %s", mem_used  / (1024*1024), (float(mem_used)  / mem_total) * 100, info["mem_used"])
    printf("mem_avail:   + %7d MB [%3d%%] %s", mem_avail / (1024*1024), (float(mem_avail) / mem_total) * 100, info["mem_avail"])
    printf("-------------- ---------- ------")
    printf("mem_total:   = %7d MB [100%%] %s", mem_total / (1024*1024), info["mem_total"])


if sys.platform.find('linux') > -1:
    linux_memory_stats()
elif sys.platform.find('darwin') > -1:
    osx_memory_stats()
elif sys.platform.find('freebsd') > -1:
    freebsd_memory_stats()

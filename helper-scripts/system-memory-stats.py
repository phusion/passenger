#!/usr/bin/python
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

if sys.platform.find('linux') > -1:
    linux_memory_stats()
elif sys.platform.find('darwin') > -1:
    osx_memory_stats()

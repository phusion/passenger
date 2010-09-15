#!/usr/bin/env ruby
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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
##############################################################################
# A script for finding owner pipe leaks in Apache. An owner pipe is considered
# to be leaked if it is owned by two or more Apache processes.
#
# This script only works on Linux. Only run it when Apache is idle.

$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'set'
require 'passenger/platform_info'

include PlatformInfo

def list_pids
	Dir.chdir("/proc") do
		return Dir["*"].select do |x|
			x =~ /^\d+$/
		end
	end
end

def list_pipes(pid)
	pipes = []
	Dir["/proc/#{pid}/fd/*"].each do |file|
		if File.symlink?(file) && File.readlink(file) =~ /^pipe:\[(.*)\]$/
			pipes << $1
		end
	end
	return pipes
end

def is_rails_app(pid)
	return File.read("/proc/#{pid}/cmdline") =~ /^Rails: /
end

def is_apache(pid)
	begin
		return File.readlink("/proc/#{pid}/exe") == HTTPD
	rescue
		return false
	end
end

# Returns a pair of these items:
# - The owner pipe map. Maps a Rails application's PID to to its owner pipe's ID.
# - The reverse map. Maps an owner pipe ID to the Rail application's PID.
def create_owner_pipe_map
	map = {}
	reverse_map = {}
	list_pids.select{ |x| is_rails_app(x) }.each do |pid|
		owner_pipe = list_pipes(pid).first
		map[pid] = owner_pipe
		reverse_map[owner_pipe] = pid
	end
	return [map, reverse_map]
end

def show_owner_pipe_listing(map, reverse_map)
	puts "------------ Owner pipe listing ------------"
	count = 0
	list_pids.select{ |x| is_apache(x) }.sort.each do |pid|
		list_pipes(pid).select do |pipe|
			reverse_map.has_key?(pipe)
		end.each do |pipe|
			puts "Apache PID #{pid} holds the owner pipe (#{pipe}) " <<
				"for Rails PID #{reverse_map[pipe]}"
			count += 1
		end
	end
	if count == 0
		puts "(none)"
	end
	puts ""
end

def show_owner_pipe_leaks(map, reverse_map)
	apache_owner_pipe_map = {}
	list_pids.select{ |x| is_apache(x) }.sort.each do |pid|
		list_pipes(pid).select do |pipe|
			reverse_map.has_key?(pipe)
		end.each do |pipe|
			apache_owner_pipe_map[pipe] ||= []
			apache_owner_pipe_map[pipe] << pid
		end
	end
	
	puts "------------ Leaks ------------"
	count = 0
	apache_owner_pipe_map.each_pair do |pipe, pids|
		if pids.size >= 2
			puts "Rails PID #{reverse_map[pipe]} owned by Apache processes: #{pids.join(", ")}"
			count += 1
		end
	end
	if count == 0
		puts "(none)"
	end
end

def start
	map, reverse_map = create_owner_pipe_map
	show_owner_pipe_listing(map, reverse_map)
	show_owner_pipe_leaks(map, reverse_map)
end

start

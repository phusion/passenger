#!/usr/bin/env ruby
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2008, 2009 Phusion
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

ESSENTIALS = [
	"boost/detail/{limits,endian}.hpp",
	"boost/config/*",
	"boost/detail/sp_counted_*",
	"boost/detail/atomic_count*",
	"libs/thread/src/*"
]
PROGRAM_SOURCE = %q{
	#include <boost/shared_ptr.hpp>
	#include <boost/thread.hpp>
	#include <boost/function.hpp>
	#include <boost/bind.hpp>
	#include <boost/date_time/posix_time/posix_time.hpp>
}
BOOST_DIR = ARGV[0]
Dir.chdir(File.dirname(__FILE__) + "/../ext")

# Run the given command, and abort on error.
def sh(*command)
	puts command.join(" ")
	if !system(*command)
		puts "*** ERROR"
		exit 1
	end
end

def install(source_filename, target_filename)
	command = ["install", "-D", "--mode=u+rw,g+r,o+r", source_filename, target_filename]
	sh(*command)
end

def copy_boost_files(*patterns)
	patterns.each do |pattern|
		Dir["#{BOOST_DIR}/#{pattern}"].each do |source|
			if File.directory?(source)
				source.slice!(0 .. BOOST_DIR.size)
				copy_boost_files("#{source}/*")
			else
				target = source.slice(BOOST_DIR.size + 1 .. source.size - 1)
				target.sub!(%r{^libs/thread/}, 'boost/')
				if !File.exist?(target)
					install(source, target)
				end
			end
		end
	end
end

def copy_essential_files
	copy_boost_files(*ESSENTIALS)
end

def prepare
	File.open("test.cpp", "w") do |f|
		f.write(PROGRAM_SOURCE)
	end
end

def cleanup
	File.unlink("test.cpp") rescue nil
end

# Compile PROGRAM_SOURCE and copy whatever missing header files the compiler needs.
def copy_dependencies
	done = false
	while !done
		missing_headers = `g++ test.cpp -c -I. 2>&1`.
		  split("\n").
		  grep(/error: .*: No such file/).
		  map do |line|
			file = line.sub(/.*error: (.*): .*/, '\1')
			if file =~ /^boost\//
				file
			else
				line =~ /(.*?):/
				source = $1
				File.dirname(source) + "/" + file
			end
		end
		missing_headers.each do |header|
			install("#{BOOST_DIR}/#{header}", header)
		end
		done = missing_headers.empty?
	end
end

def start
	if BOOST_DIR.nil? || BOOST_DIR.empty?
		puts "Usage: copy_boost_headers.rb <boost source directory>"
		exit 1
	end
	begin
		prepare
		copy_essential_files
		copy_dependencies
	ensure
		cleanup
	end
end

start

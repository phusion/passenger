#!/usr/bin/env ruby
PROGRAM_SOURCE = %q{
	#include <boost/shared_ptr.hpp>
	#include <boost/thread.hpp>
	#include <boost/function.hpp>
	#include <boost/bind.hpp>
	#include <boost/date_time/posix_time/posix_time.hpp>
}

boost_dir = ARGV[0]
Dir.chdir(File.dirname(__FILE__) + "/../ext")
File.open("test.cpp", "w") do |f|
	f.write(PROGRAM_SOURCE)
end

def sh(*command)
	puts command.join(" ")
	if !system(*command)
		puts "*** ERROR"
		exit 1
	end
end

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
		command = ["install", "-D", "--mode=u+rw,g+r,o+r", "#{boost_dir}/#{header}", header]
		sh(*command)
	end
	done = missing_headers.empty?
end

sh "cp    #{boost_dir}/boost/detail/{limits,endian}.hpp boost/detail/"
sh "cp -R #{boost_dir}/boost/config/* boost/config/"
sh "cp -R #{boost_dir}/boost/detail/sp_counted_* boost/detail/"
sh "cp -R #{boost_dir}/boost/detail/atomic_count* boost/detail/"
sh "mkdir -p boost/src && cp -R #{boost_dir}/libs/thread/src/* boost/src/"

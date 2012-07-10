#!/usr/bin/env ruby
# Parses output of the glibc backtrace() function and
# translates addresses into function names and source
# file locations.

input = STDIN
output = STDERR
argv0, pid = ARGV
exe_filename = File.expand_path(File.readlink("/proc/#{pid}/exe"))
exe_basename = File.basename(exe_filename)

while !input.eof?
	line = input.readline.strip
	if line =~ /(.*)\[(.*?)\]$/
		subject = $1
		address = $2
		subject.strip!
		subject =~ /(.*)(\((.*?)\))?$/
		file = $1
		context = $3
		file.strip!
		context = nil if context && context.empty?

		if file =~ /\A\// && File.exist?(file)
			filename = file
		elsif file == argv0 || file == exe_filename || file == exe_basename
			filename = exe_filename
		else
			filename = nil
		end
		if filename.nil?
			output.puts line
		else
			function, source = `addr2line -Cfie "#{filename}" #{address}`.split("\n")
			new_context = "#{function} at #{source}"
			new_context << "; #{context}" if context
			if new_context.include?("??")
				
			end
			output.puts "#{file}(#{new_context}) [#{address}]"
		end
	else
		output.puts line
	end
end

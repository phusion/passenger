#!/usr/bin/env ruby
# Parses output of the glibc backtrace() function and
# translates addresses into function names and source
# file locations.

class Addr2line
	attr_reader :stdin, :stdout, :pid

	def initialize(filename)
		a, b = IO.pipe
		c, d = IO.pipe
		@pid = fork do
			STDIN.reopen(a)
			b.close
			STDOUT.reopen(d)
			c.close
			exec("addr2line", "-Cfie", filename)
		end
		a.close
		d.close
		@stdin = b
		@stdout = c
		@stdin.sync = @stdout.sync = true
	end

	def query(address)
		@stdin.write("#{address}\n")
		function = @stdout.readline
		source = @stdout.readline
		function.strip!
		source.strip!
		return [function, source]
	end

	def close
		@stdin.close
		@stdout.close
		Process.kill('KILL', @pid)
		Process.waitpid(@pid) rescue nil
	end
end

input = STDIN
output = STDERR
argv0, pid_or_filename = ARGV
if pid_or_filename =~ /\A\d+\Z/
	exe_filename = File.expand_path(File.readlink("/proc/#{pid_or_filename}/exe"))
else
	exe_filename = File.expand_path(pid_or_filename)
end
exe_basename = File.basename(exe_filename)
addr2lines = {}

begin
	while !input.eof?
		line = input.readline.strip
		# Example lines:
		# ./test() [0x400b64]
		# /lib/libc.so.6(__libc_start_main+0xfd) [0x7fcc0ad00c8d]
		if line =~ /(.*)\[(.*?)\]$/
			# Split line into:
			# subject: /lib/libc.so.6(__libc_start_main+0xfd)
			# address: 0x7fcc0ad00c8d
			subject = $1
			address = $2
			subject.strip!
			subject =~ /(.*?)(\((.*?)\))?$/
			# Split subject into:
			# file: /lib/libc.so.6
			# context: __libc_start_main+0xfd
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
				addr2line = (addr2lines[filename] ||= Addr2line.new(filename))
				function, source = addr2line.query(address)
				new_context = "#{function} at #{source}"
				new_context << "; #{context}" if context
				output.puts "#{file}(#{new_context}) [#{address}]"
			end
		else
			output.puts line
		end
	end
ensure
	addr2lines.each_value do |addr2line|
		addr2line.close
	end
end

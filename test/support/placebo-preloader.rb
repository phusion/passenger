#!/usr/bin/env ruby
# An application preloader which doesn't actually preload anything.
DIR = File.expand_path(File.dirname(__FILE__))
require "#{DIR}/../../lib/phusion_passenger"
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/native_support'
require 'socket'

STDOUT.sync = true
STDERR.sync = true
puts "I have control 1.0"
abort "Invalid initialization header" if STDIN.readline != "You have control 1.0\n"

options = {}
while (line = STDIN.readline) != "\n"
	name, value = line.strip.split(/: */, 2)
	options[name] = value
end

socket_filename = "/tmp/placebo-preloader.sock.#{Process.pid}"
server = UNIXServer.new(socket_filename)
puts "Ready"
puts "socket: unix:#{socket_filename}"
puts

def process_client_command(client, command)
	if command == "spawn\n"
		options = {}
		while (line = client.readline) != "\n"
			name, value = line.strip.split(/: */, 2)
			options[name] = value
		end
		
		command = options["start_command"].split("\1")
		process_title = options["process_title"]
		process_title = command[0] if !process_title || process_title.empty?
		command[0] = [command[0], process_title]
		
		a, b = UNIXSocket.pair
		if Process.respond_to?(:spawn)
			spawn_options = command.dup
			spawn_options << {
				:in => a,
				:out => a,
				:err => :err,
				:close_others => true
			}
			pid = Process.spawn(*spawn_options)
		else
			pid = fork do
				begin
					STDIN.reopen(a)
					STDOUT.reopen(a)
					if defined?(PhusionPassenger::NativeSupport)
						PhusionPassenger::NativeSupport.close_all_file_descriptors([0, 1, 2])
					end
					exec(*command)
				rescue Exception => e
					STDERR.puts "*** ERROR: #{e}\n#{e.backtrace.join("\n")}"
				ensure
					STDERR.flush
					exit!(1)
				end
			end
		end
		a.close
		#Process.detach(pid)
		
		client.write("OK\n")
		client.write("#{pid}\n")
		client.readline
		client.send_io(b)
		client.readline
		
		b.close
	elsif command == "pid\n"
		client.write("#{Process.pid}\n")
	else
		client.write("unknown request\n")
	end
end

begin
	exit if ARGV[0] == "exit-immediately"
	while true
		ios = select([server, STDIN])[0]
		if ios.include?(server)
			client = server.accept
			begin
				process_client_command(client, client.readline)
			ensure
				client.close
			end
		end
		if ios.include?(STDIN)
			begin
				STDIN.readline
			rescue EOFError
				exit
			end
		end
	end
ensure
	File.unlink(socket_filename) rescue nil
end

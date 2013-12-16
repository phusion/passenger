#!/usr/bin/env ruby
# An application preloader which doesn't actually preload anything
# and executes the requested start command.

DIR = File.expand_path(File.dirname(__FILE__))
require File.expand_path("#{DIR}/../../lib/phusion_passenger")
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'native_support'
require 'socket'

STDOUT.sync = true
STDERR.sync = true
puts "!> I have control 1.0"
abort "Invalid initialization header" if STDIN.readline != "You have control 1.0\n"

options = {}
while (line = STDIN.readline) != "\n"
	name, value = line.strip.split(/: */, 2)
	options[name] = value
end

socket_filename = "/tmp/placebo-preloader.sock.#{Process.pid}"
server = UNIXServer.new(socket_filename)
puts "!> Ready"
puts "!> socket: unix:#{socket_filename}"
puts "!> "

def process_client_command(server, client, command)
	if command == "spawn\n"
		options = {}
		while (line = client.readline) != "\n"
			name, value = line.strip.split(/: */, 2)
			options[name] = value
		end
		
		command = options["start_command"].split("\t")
		process_title = options["process_title"]
		process_title = command[0] if !process_title || process_title.empty?
		command[0] = [command[0], process_title]
		
		pid = fork do
			begin
				STDIN.reopen(client)
				STDOUT.reopen(client)
				STDOUT.sync = true
				server.close
				client.close
				puts "OK"
				puts Process.pid
				exec(*command)
			rescue Exception => e
				STDERR.puts "*** ERROR: #{e}\n#{e.backtrace.join("\n")}"
			ensure
				STDERR.flush
				exit!(1)
			end
		end
		Process.detach(pid)
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
				process_client_command(server, client, client.readline)
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

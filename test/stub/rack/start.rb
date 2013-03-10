#!/usr/bin/env ruby
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

if ARGV[0] == "--execself"
	# Used for testing https://code.google.com/p/phusion-passenger/issues/detail?id=842#c19
	exec("ruby", $0)
end

server = TCPServer.new('127.0.0.1', 0)
puts "!> Ready"
puts "!> socket: main;tcp://127.0.0.1:#{server.addr[1]};session;1"
puts "!> "

while true
	ios = select([server, STDIN])[0]
	if ios.include?(server)
		client = server.accept
		line = client.readline
		if line == "ping\n"
			client.write("pong\n")
		elsif line == "pid\n"
			client.write("#{Process.pid}\n")
		elsif line == "envvars\n"
			str = ""
			ENV.each_pair do |key, value|
				str << "#{key} = #{value}\n"
			end
			client.write(str)
		else
			client.write("unknown request\n")
		end
		client.close
	end
	if ios.include?(STDIN)
		begin
			STDIN.readline
		rescue EOFError
			exit
		end
	end
end

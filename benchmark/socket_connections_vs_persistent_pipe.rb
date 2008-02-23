#!/usr/bin/env ruby
# We've tried to implement the usage of persistent pipes in Passenger,
# but we found out that it made Apache slower than when we use socket
# connections. That is in contrast to the results of this benchmark.
#
# We couldn't have used persistent pipes (for multiple sessions) in
# Passenger anyway, because it's too fragile. If something goes wrong
# in one session, then the entire pipe becomes unusable for subsequent
# sessions because the protocol will be in an undefined state. Sockets
# don't have this problem because if an error occurs because each socket
# connection belongs to exactly one session.
require 'benchmark'

ITERATIONS = 100000
MESSAGE = " " * 512
MESSAGE_SIZE = [MESSAGE.size].pack('n')

def start
	if ARGV.empty?
		benchmark_unix_sockets
		benchmark_persistent_pipes
	elsif ARGV.size == 1 && ARGV[0] == 'unix_sockets'
		benchmark_unix_sockets
	elsif ARGV.size == 1 && ARGV[0] == 'pipes'
		benchmark_persistent_pipes
	else
		puts "Benchmarks performance ."
		puts "Usage: socket_connections_vs_persistent_pipes.rb <unix_sockets|pipes>"
		exit(1)
	end
end

def benchmark_unix_sockets
	require 'socket'
	begin
		puts "Benchmarking socket connections..."
		File.unlink("benchmark.socket") rescue nil
		pid = fork do
			server = UNIXServer.new("benchmark.socket")
			ITERATIONS.times do
				client = server.accept
				size = client.read(2).unpack('n')[0]
				client.read(size)
				client.write(MESSAGE)
				client.close
			end
			server.close
		end
		result = Benchmark.measure do
			ITERATIONS.times do
				conn = UNIXSocket.new("benchmark.socket")
				conn.write(MESSAGE_SIZE)
				conn.write(MESSAGE)
				conn.read
				conn.close
			end
		end
		Process.waitpid(pid)
		puts "User/system/real time: #{result}"
		printf "%.2f messages per second\n", ITERATIONS / result.real
	ensure
		File.unlink("benchmark.socket") rescue nil
	end
end

def benchmark_persistent_pipes
	puts "Benchmarking pipes connections..."
	reader1, writer1 = IO.pipe
	reader2, writer2 = IO.pipe
	pid = fork do
		reader1.close
		writer2.close
		reader = reader2
		writer = writer1
		ITERATIONS.times do
			size = reader.read(2).unpack('n')[0]
			reader.read(size)
			writer.write(MESSAGE_SIZE)
			writer.write(MESSAGE)
		end
	end
	reader2.close
	writer1.close
	reader = reader1
	writer = writer2
	result = Benchmark.measure do
		ITERATIONS.times do
			writer.write(MESSAGE_SIZE)
			writer.write(MESSAGE)
			size = reader.read(2).unpack('n')[0]
			reader.read(size)
		end
	end
	Process.waitpid(pid)
	puts "User/system/real time: #{result}"
	printf "%.2f messages per second\n", ITERATIONS / result.real
end

start

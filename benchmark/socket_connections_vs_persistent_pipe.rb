#!/usr/bin/env ruby
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
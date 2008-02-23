#!/usr/bin/env ruby
require 'benchmark'
require 'socket'

ITERATIONS = 100000
MESSAGE = " " * 512
MESSAGE_SIZE = [MESSAGE.size].pack('n')

def start
	if ARGV.empty?
		benchmark_accept
		benchmark_socketpair
	elsif ARGV.size == 1 && ARGV[0] == 'accept'
		benchmark_accept
	elsif ARGV.size == 1 && ARGV[0] == 'socketpair'
		benchmark_socketpair
	else
		puts "Usage: accept_vs_socketpair.rb <accept|socketpair>"
		exit(1)
	end
end

def benchmark_accept
	begin
		puts "Benchmarking accept()..."
		File.unlink("benchmark.socket") rescue nil
		pid = fork do
			server = UNIXServer.new("benchmark.socket")
			ITERATIONS.times do
				client = server.accept
				client.readline
				client.close
			end
			server.close
		end
		result = Benchmark.measure do
			ITERATIONS.times do
				conn = UNIXSocket.new("benchmark.socket")
				conn.puts("hello")
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

def benchmark_socketpair
	puts "Benchmarking socketpair()..."
	listener, connector = UNIXSocket.pair
	pid = fork do
		connector.close
		ITERATIONS.times do
			a, b = UNIXSocket.pair
			listener.send_io(a)
			a.close
			b.readline
			b.close
		end
		listener.close
	end
	result = Benchmark.measure do
		listener.close
		ITERATIONS.times do
			conn = connector.recv_io
			conn.puts("hello")
			conn.close
		end
		connector.close
	end
	Process.waitpid(pid)
	puts "User/system/real time: #{result}"
	printf "%.2f messages per second\n", ITERATIONS / result.real
end

start
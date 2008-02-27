#!/usr/bin/env ruby
require 'benchmark'
require 'socket'
require 'dl/import'

ITERATIONS = 100000
# Constants to relieve the garbage collector
MESSAGE = " " * 512
MESSAGE_SIZE = [MESSAGE.size].pack('n')
FIFO = 'fifo'
R = 'r'
W = 'w'

def start
	if ARGV.empty?
		benchmark_accept
		benchmark_socketpair
		benchmark_named_pipes
	elsif ARGV.size == 1 && ARGV[0] == 'accept'
		benchmark_accept
	elsif ARGV.size == 1 && ARGV[0] == 'socketpair'
		benchmark_socketpair
	elsif ARGV.size == 1 && ARGV[0] == 'named_pipes'
		benchmark_named_pipes
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

def benchmark_named_pipes
	puts "Benchmarking named pipes..."
	listener, connector = UNIXSocket.pair
	pid = fork do
		connector.close
		ITERATIONS.times do
			LIBC.mkfifo(FIFO, 0600)
			listener.write("#{FIFO}\n")
			File.open(FIFO, R) do |f|
				f.readline
			end
			File.unlink(FIFO)
		end
		listener.close
	end
	result = Benchmark.measure do
		listener.close
		ITERATIONS.times do
			fifo_name = connector.readline
			fifo_name.rstrip!
			File.open(fifo_name, W) do |f|
				f.puts("hello")
			end
		end
		connector.close
	end
	Process.waitpid(pid)
	puts "User/system/real time: #{result}"
	printf "%.2f messages per second\n", ITERATIONS / result.real
end

# Urgh, Ruby does not support mkfifo...
module LIBC
	extend DL::Importable
	dlload 'libc.so.6'
	extern 'int mkfifo(char*, uint)'
	extern 'void perror(char*)'
end

start

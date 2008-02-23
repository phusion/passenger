#!/usr/bin/env ruby
require 'benchmark'
require 'socket'

ITERATIONS = 100000
REQUEST = " " * 512
REQUEST_SIZE = REQUEST.size
RESPONSE = " " * 2048
RESPONSE_SIZE = RESPONSE.size
PASSWORD = "x" * 128
PASSWORD_SIZE = PASSWORD.size

def start
	benchmark_with_password_checking
	puts ""
	benchmark_without_password_checking
end

def benchmark_with_password_checking
	puts "Benchmarking with password checking..."
	parent, child = UNIXSocket.pair
	pid = fork do
		parent.close
		ITERATIONS.times do
			password = child.read(PASSWORD_SIZE)
			if password == PASSWORD
				child.read(REQUEST_SIZE)
				child.write(RESPONSE)
			else
				child.close
				break
			end
		end
	end
	child.close
	
	result = Benchmark.measure do
		ITERATIONS.times do
			parent.write(PASSWORD)
			parent.write(REQUEST)
			parent.read(RESPONSE_SIZE)
		end
	end
	Process.waitpid(pid)
	puts "User/system/real time: #{result}"
	printf "%.2f messages per second\n", ITERATIONS / result.real
end

def benchmark_without_password_checking
	puts "Benchmarking without password checking..."
	parent, child = UNIXSocket.pair
	pid = fork do
		parent.close
		ITERATIONS.times do
			child.read(REQUEST_SIZE)
			child.write(RESPONSE)
		end
	end
	child.close
	
	result = Benchmark.measure do
		ITERATIONS.times do
			parent.write(REQUEST)
			parent.read(RESPONSE_SIZE)
		end
	end
	Process.waitpid(pid)
	puts "Without password checking:"
	puts "User/system/real time: #{result}"
	printf "%.2f messages per second\n", ITERATIONS / result.real
end

start

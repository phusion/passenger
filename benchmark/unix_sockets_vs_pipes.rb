#!/usr/bin/env ruby
# Benchmarks raw Unix socket I/O performance versus pipe I/O performance.
require 'benchmark'

ITERATIONS = 150000

def start
	if ARGV.empty?
		benchmark(:unix_sockets)
		benchmark(:pipes)
	elsif ARGV.size == 1 && ARGV[0] == 'unix_sockets'
		benchmark(:unix_sockets)
	elsif ARGV.size == 1 && ARGV[0] == 'pipes'
		benchmark(:pipes)
	else
		puts "Benchmarks raw Unix socket I/O performance versus pipe I/O performance."
		puts "Usage: unix_sockets_vs_pipes.rb <unix_sockets|pipes>"
		exit(1)
	end
end

def benchmark(type)
	if type == :unix_sockets
		puts "Benchmarking Unix sockets..."
		pid, reader, writer = setup_unix_sockets
	else
		puts "Benchmarking pipes..."
		pid, reader, writer = setup_pipes
	end
	result = Benchmark.measure do
		ITERATIONS.times do |i|
			writer.write("hello world\n")
			reader.readline
		end
		reader.close
		if reader != writer
			writer.close
		end
		Process.waitpid(pid)
	end
	puts "User/system/real time: #{result}"
	printf "%.2f lines per second\n", ITERATIONS / result.real
end

def setup_unix_sockets
	require 'socket'
	a, b = UNIXSocket.pair
	pid = fork do
		a.close
		child_main_loop(b, b)
	end
	b.close
	return [pid, a, a]
end

def setup_pipes
	a, b = IO.pipe
	c, d = IO.pipe
	pid = fork do
		b.close
		c.close
		reader = a
		writer = d
		child_main_loop(reader, writer)
	end
	a.close
	d.close
	reader = c
	writer = b
	return [pid, reader, writer]
end

def child_main_loop(reader, writer)
	begin
		while true
			x = reader.readline
			writer.write(x)
		end
	rescue EOFError
	end
end

start
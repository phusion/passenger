#!/usr/bin/env ruby
# Benchmark raw speed of the Rails dispatcher.
PASSENGER_ROOT = File.expand_path("#{File.dirname(__FILE__)}/..")
$LOAD_PATH << "#{PASSENGER_ROOT}/lib"
ENV["RAILS_ENV"] = "production"

require 'yaml'
require 'benchmark'
require 'passenger/request_handler'
require 'config/environment'
require 'dispatcher'

include Passenger

class OutputChannel
	def write(data)
		# Black hole
	end
end

def start(iterations)
	headers = YAML.load_file("#{PASSENGER_ROOT}/test/stub/http_request.yml")
	output = OutputChannel.new
	milestone = iterations / 10
	milestone = 1 if milestone == 0
	result = Benchmark.measure do
		iterations.times do |i|
			cgi = CGIFixed.new(headers, output, STDOUT)
			::Dispatcher.dispatch(cgi,
				::ActionController::CgiRequest::DEFAULT_SESSION_OPTIONS,
				cgi.stdoutput)
			if i % milestone == 0 && i != 0
				puts "Completed #{i} requests"
			end
		end
	end
	puts "#{iterations} requests: #{result}"
	printf "Speed: %.2f requests/sec\n", iterations / result.total.to_f
end

puts "Benchmark started."
t = Thread.new do
	#start(ARGV[0] ? ARGV[0].to_i : 1000)
end
start(ARGV[0] ? ARGV[0].to_i : 1000)
t.join

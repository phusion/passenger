if GC.respond_to?(:copy_on_write_friendly?) && !GC.copy_on_write_friendly?
	GC.copy_on_write_friendly = true
end

RUBY_VERSION_INT = RUBY_VERSION.split('.')[0..2].join.to_i

source_root = File.expand_path(File.dirname(__FILE__) + "/../..")
Dir.chdir("#{source_root}/test")

require 'rubygems'
require 'json'
begin
	CONFIG = JSON.load(File.read('config.json'))
rescue Errno::ENOENT
	STDERR.puts "*** You do not have the file test/config.json. " <<
		"Please copy test/config.json.example to " <<
		"test/config.json, and edit it."
	exit 1
end

def boolean_option(name, default_value = false)
	value = ENV[name]
	if value.nil? || value.empty?
		return default_value
	else
		return value == "yes" || value == "on" || value == "true" || value == "1"
	end
end

DEBUG = boolean_option('DEBUG')
TEST_CLASSIC_RAILS = boolean_option('TEST_CLASSIC_RAILS', Gem::VERSION <= '1.9')

ENV.delete('PASSENGER_DEBUG')

$LOAD_PATH.unshift("#{source_root}/lib")
$LOAD_PATH.unshift("#{source_root}/test")

require 'thread'
require 'timeout'
require 'fileutils'
require 'phusion_passenger'
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'debug_logging'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'utils/tmpdir'
require 'support/test_helper'

include TestHelper

# Seed the pseudo-random number generator here
# so that it doesn't happen in the child processes.
srand

trap "QUIT" do
	STDERR.puts PhusionPassenger::Utils.global_backtrace_report
end

class DeadlineTimer
	def initialize(main_thread, deadline)
		@mutex = Mutex.new
		@cond  = ConditionVariable.new
		@iteration = 0
		@pipe  = IO.pipe

		@thread = Thread.new do
			Thread.current.abort_on_exception = true
			expected_iteration = 1
			ios = [@pipe[0]]
			while true
				@mutex.synchronize do
					while @iteration != expected_iteration
						@cond.wait(@mutex)
					end
				end
				if !select(ios, nil, nil, deadline)
					STDERR.puts "*** Test timed out (#{deadline} seconds)"
					STDERR.puts PhusionPassenger::Utils.global_backtrace_report
					main_thread.raise(Timeout::Error, "Test timed out")
					expected_iteration += 1
				elsif @pipe[0].read(1).nil?
					break
				else
					expected_iteration += 1
				end
			end
		end
	end

	def start
		@mutex.synchronize do
			@iteration += 1
			@cond.signal
		end
	end

	def stop
		@pipe[1].write('x')
	end
end

DEADLINE_TIMER = DeadlineTimer.new(Thread.current, 30)

RSpec.configure do |config|
	config.before(:each) do
		# Suppress warning messages.
		PhusionPassenger::DebugLogging.log_level = -1
		PhusionPassenger::DebugLogging.log_file = nil
		PhusionPassenger::DebugLogging.stderr_evaluator = nil
		
		# Create the temp directory.
		PhusionPassenger::Utils.passenger_tmpdir

		DEADLINE_TIMER.start
	end
	
	config.after(:each) do
		tmpdir = PhusionPassenger::Utils.passenger_tmpdir(false)
		if File.exist?(tmpdir)
			remove_dir_tree(tmpdir)
		end
		DEADLINE_TIMER.stop
	end
end

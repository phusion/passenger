if GC.respond_to?(:copy_on_write_friendly?) && !GC.copy_on_write_friendly?
	GC.copy_on_write_friendly = true
end

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

DEBUG      = ['1', 'y', 'yes'].include?(ENV['DEBUG'].to_s.downcase)
AGENTS_DIR = "#{source_root}/agents"

$LOAD_PATH.unshift("#{source_root}/lib")
$LOAD_PATH.unshift("#{source_root}/test")

require 'fileutils'
require 'support/test_helper'
require 'phusion_passenger'
PhusionPassenger.locate_directories
require 'phusion_passenger/debug_logging'
require 'phusion_passenger/utils/tmpdir'

include TestHelper

# Seed the pseudo-random number generator here
# so that it doesn't happen in the child processes.
srand

trap "QUIT" do
	puts caller
end

Spec::Runner.configure do |config|
	config.append_before do
		# Suppress warning messages.
		PhusionPassenger::DebugLogging.log_level = -1
		PhusionPassenger::DebugLogging.log_file = nil
		PhusionPassenger::DebugLogging.stderr_evaluator = nil
		
		# Create the temp directory.
		PhusionPassenger::Utils.passenger_tmpdir
	end
	
	config.append_after do
		tmpdir = PhusionPassenger::Utils.passenger_tmpdir(false)
		if File.exist?(tmpdir)
			remove_dir_tree(tmpdir)
		end
	end
end

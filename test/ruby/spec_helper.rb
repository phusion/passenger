source_root = File.expand_path(File.dirname(__FILE__) + "/../..")
Dir.chdir("#{source_root}/test")

require 'yaml'
begin
	CONFIG = YAML::load_file('config.yml')
rescue Errno::ENOENT
	STDERR.puts "*** You do not have the file test/config.yml. " <<
		"Please copy test/config.yml.example to " <<
		"test/config.yml, and edit it."
	exit 1
end

$LOAD_PATH.unshift("#{source_root}/lib")
$LOAD_PATH.unshift("#{source_root}/ext")
$LOAD_PATH.unshift("#{source_root}/test")

require 'fileutils'
require 'support/test_helper'
require 'phusion_passenger/utils'

include PhusionPassenger
include TestHelper

# Seed the pseudo-random number generator here
# so that it doesn't happen in the child processes.
srand

trap "QUIT" do
	puts caller
end

Spec::Runner.configure do |config|
	config.append_before do
		# Create the temp directory.
		PhusionPassenger::Utils.passenger_tmpdir
	end
	
	config.append_after do
		tmpdir = PhusionPassenger::Utils.passenger_tmpdir(false)
		if File.exist?(tmpdir)
			FileUtils.chmod_R(0777, tmpdir)
			FileUtils.rm_rf(tmpdir)
		end
	end
end
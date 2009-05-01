root = File.expand_path("#{File.dirname(__FILE__)}/../..")
Dir.chdir("#{root}/test")
$LOAD_PATH.unshift("#{root}/lib", "#{root}/ext")
require 'yaml'

begin
	CONFIG = YAML::load_file('config.yml')
rescue Errno::ENOENT
	STDERR.puts "*** You do not have the file test/config.yml. " <<
		"Please copy test/config.yml.example to " <<
		"test/config.yml, and edit it."
	exit 1
end

require 'fileutils'
require 'phusion_passenger/utils'

# Calculate location of the temp dir and cache it.
PhusionPassenger::Utils.passenger_tmpdir

Spec::Runner.configure do |config|
	config.append_after do
		FileUtils.chmod_R(0777, PhusionPassenger::Utils.passenger_tmpdir);
		FileUtils.rm_rf(PhusionPassenger::Utils.passenger_tmpdir)
	end
end
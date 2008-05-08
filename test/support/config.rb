$LOAD_PATH << File.expand_path("#{File.dirname(__FILE__)}/../../lib")
require 'yaml'
require 'passenger/passenger'

begin
	CONFIG = YAML::load_file('config.yml')
rescue Errno::ENOENT
	STDERR.puts "*** You do not have the file test/config.yml. " <<
		"Please copy test/config.yml.example to " <<
		"test/config.yml, and edit it."
	exit 1
end

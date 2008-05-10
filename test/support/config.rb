$LOAD_PATH.unshift(File.expand_path("#{File.dirname(__FILE__)}/../../lib"))
$LOAD_PATH.unshift(File.expand_path("#{File.dirname(__FILE__)}/../../ext"))
require 'yaml'

begin
	CONFIG = YAML::load_file('config.yml')
rescue Errno::ENOENT
	STDERR.puts "*** You do not have the file test/config.yml. " <<
		"Please copy test/config.yml.example to " <<
		"test/config.yml, and edit it."
	exit 1
end

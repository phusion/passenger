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
$LOAD_PATH.unshift("#{source_root}/test")

require 'phusion_passenger'
require 'support/test_helper'
include TestHelper

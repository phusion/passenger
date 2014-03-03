source_root = File.expand_path(File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
require 'phusion_passenger/packaging'

Gem::Specification.new do |s|
	s.platform = Gem::Platform::RUBY
	s.homepage = "https://www.phusionpassenger.com/"
	s.summary = "A fast and robust web server and application server for Ruby, Python and Node.js"
	s.name = PhusionPassenger::PACKAGE_NAME
	s.version = PhusionPassenger::VERSION_STRING
	s.rubyforge_project = "passenger"
	s.author = "Phusion - http://www.phusion.nl/"
	s.email = "software-signing@phusion.nl"
	s.require_paths = ["lib"]
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'daemon_controller', '>= 1.2.0'
	s.add_dependency 'rack'
	s.files = Dir[*PhusionPassenger::Packaging::GLOB] -
		Dir[*PhusionPassenger::Packaging::EXCLUDE_GLOB]
	s.executables = PhusionPassenger::Packaging::USER_EXECUTABLES +
		PhusionPassenger::Packaging::SUPER_USER_EXECUTABLES
	s.description = "A modern web server and application server for Ruby, Python and Node.js, " +
		"optimized for performance, low memory usage and ease of use."

	if ENV['OFFICIAL_RELEASE']
		s.extensions = ["helper-scripts/download_binaries/extconf.rb"]
	end
end

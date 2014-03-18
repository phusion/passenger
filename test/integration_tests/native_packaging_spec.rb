# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2014 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

LOCATIONS_INI = ENV['LOCATIONS_INI']
abort "Please set the LOCATIONS_INI environment variable to the right locations.ini" if !LOCATIONS_INI

NATIVE_PACKAGING_METHOD = ENV['NATIVE_PACKAGING_METHOD']
if !["deb", "rpm", "homebrew"].include?(NATIVE_PACKAGING_METHOD)
	abort "Please set NATIVE_PACKAGING_METHOD to either 'deb', 'rpm' or 'homebrew'"
end

source_root = File.expand_path("../..", File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'config/validate_install_command'
require 'tmpdir'
require 'fileutils'
require 'open-uri'
require 'pathname'


# Ensure that the natively installed tools are in PATH.
ENV['PATH'] = "/usr/bin:/usr/sbin:#{ENV['PATH']}"
# Force Rake to redirect stderr to stdout so that we can capture all output.
ENV['STDERR_TO_STDOUT'] = '1'


module PhusionPassenger

case NATIVE_PACKAGING_METHOD
when "deb"
	BINDIR = "/usr/bin"
	SBINDIR = "/usr/sbin"
	INCLUDEDIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/include"
	NGINX_ADDON_DIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/ngx_http_passenger_module"
	DOCDIR = "/usr/share/doc/passenger"
	HELPER_SCRIPTS_DIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/helper-scripts"
	RUBY_EXTENSION_SOURCE_DIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/ruby_extension_source"
	AGENTS_DIR = "/usr/lib/#{GLOBAL_NAMESPACE_DIRNAME}/agents"
	APACHE2_MODULE_PATH = "/usr/lib/apache2/modules/mod_passenger.so"
	SUPPORTS_COMPILING_APACHE_MODULE = false

	APXS2 = "/usr/bin/apxs2"
	APACHE2 = "/usr/sbin/apache2"
	APACHE2CTL = "/usr/sbin/apache2ctl"
	APACHE_CONFIG_FILE = "/etc/apache2/apache2.conf"
	APACHE_ERROR_LOG = "/var/log/apache2/error.log"
when "rpm"
	BINDIR = "/usr/bin"
	SBINDIR = "/usr/sbin"
	INCLUDEDIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/include"
	NGINX_ADDON_DIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/ngx_http_passenger_module"
	DOCDIR = "/usr/share/doc/passenger"
	HELPER_SCRIPTS_DIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/helper-scripts"
	RUBY_EXTENSION_SOURCE_DIR = "/usr/share/#{GLOBAL_NAMESPACE_DIRNAME}/ruby_extension_source"
	AGENTS_DIR = "/usr/lib64/#{GLOBAL_NAMESPACE_DIRNAME}/agents"
	APACHE2_MODULE_PATH = "/usr/lib64/httpd/modules/mod_passenger.so"
	SUPPORTS_COMPILING_APACHE_MODULE = false

	APXS2 = "/usr/sbin/apxs"
	APACHE2 = "/usr/sbin/httpd"
	APACHE2CTL = "/usr/sbin/apachectl"
	APACHE_CONFIG_FILE = "/etc/httpd/conf/httpd.conf"
	APACHE_ERROR_LOG = "/etc/httpd/logs/error_log"
when "homebrew"
	# Ensure that the Homebrew-installed Phusion Passenger is the first in PATH.
	ENV['PATH'] = "/usr/local/bin:#{ENV['PATH']}"
	root = "/usr/local/Cellar/passenger/#{VERSION_STRING}/libexec"

	BINDIR = "#{root}/bin"
	SBINDIR = BINDIR
	INCLUDEDIR = "#{root}/ext"
	NGINX_ADDON_DIR = "#{root}/ext/nginx"
	DOCDIR = "#{root}/doc"
	HELPER_SCRIPTS_DIR = "#{root}/helper-scripts"
	RUBY_EXTENSION_SOURCE_DIR = "#{root}/ext/ruby"
	AGENTS_DIR = "#{root}/buildout/agents"
	APACHE2_MODULE_PATH = "#{root}/buildout/apache2/mod_passenger.so"
	SUPPORTS_COMPILING_APACHE_MODULE = true

	APXS2 = "/usr/sbin/apxs"
	APACHE2 = "/usr/sbin/httpd"
	APACHE2CTL = "/usr/sbin/apachectl"
	APACHE_CONFIG_FILE = "/private/etc/apache2/httpd.conf"
	APACHE_ERROR_LOG = "/private/var/log/apache2/error_log"
end

describe "A natively packaged Phusion Passenger" do
	def capture_output(command)
		output = `#{command}`.strip
		if $?.exitstatus == 0
			return output
		else
			filename = `mktemp /tmp/output.XXXXXX`.strip
			File.open(filename, "w") do |f|
				f.write(output)
			end
			abort "Command #{command} exited with status #{$?.exitstatus}. Output written to #{filename}"
		end
	end

	def which(command)
		return capture_output("which #{command}")
	end

	def realpath(path)
		Pathname.new(path).realpath.to_s
	end

	def sh(*command)
		if !system(*command)
			abort "Command failed: #{command.join(' ')}"
		end
	end

	def install_apache2_module
		orig_mtime = File.stat(APACHE2_MODULE_PATH).mtime
		output = capture_output("passenger-install-apache2-module --auto 2>&1")
		output.should include("Almost there!")
		output.should include("LoadModule passenger_module #{APACHE2_MODULE_PATH}\n")
		output.should include("PassengerRoot #{LOCATIONS_INI}\n")
		File.stat(APACHE2_MODULE_PATH).mtime.should_not == orig_mtime
	end

	def install_nginx_module
		Dir.mktmpdir do |path|
			output = capture_output("passenger-install-nginx-module --auto --prefix=#{path} --auto-download 2>&1")
			output.should include("passenger_root #{LOCATIONS_INI};")
			File.exist?("#{path}/sbin/nginx").should be_true
		end
	end

	specify "locations.ini only refers to existent filesystem locations" do
		File.read(LOCATIONS_INI).split("\n").each do |line|
			if line =~ /=/
				name, filename = line.split('=', 2)
				if filename =~ /^\// && !File.exist?(filename)
					raise "#{filename} does not exist"
				end
			end
		end
	end

	specify "locations.ini sets native_packaging_method to #{NATIVE_PACKAGING_METHOD}" do
		File.read(LOCATIONS_INI).should =~ /^native_packaging_method=#{NATIVE_PACKAGING_METHOD}$/
	end

	specify "passenger-status is in #{SBINDIR}" do
		realpath(which("passenger-status")).should == "#{SBINDIR}/passenger-status"
	end

	specify "the Nginx runtime library headers exist" do
		File.directory?(INCLUDEDIR).should be_true
		Dir["#{INCLUDEDIR}/common/*.h"].should_not be_empty
	end

	specify "the Nginx addon directory exists" do
		File.directory?(NGINX_ADDON_DIR).should be_true
		File.file?("#{NGINX_ADDON_DIR}/ngx_http_passenger_module.c")
	end

	specify "the documentation directory exists" do
		File.directory?(DOCDIR).should be_true
		File.file?("#{DOCDIR}/Users guide Apache.html").should be_true
	end

	specify "the helper-scripts directory exists" do
		File.directory?(HELPER_SCRIPTS_DIR).should be_true
		File.file?("#{HELPER_SCRIPTS_DIR}/rack-loader.rb").should be_true
	end

	specify "the Ruby extension source directory exists" do
		File.directory?(RUBY_EXTENSION_SOURCE_DIR).should be_true
		File.file?("#{RUBY_EXTENSION_SOURCE_DIR}/extconf.rb").should be_true
	end

	specify "the agents directory exists" do
		File.directory?(AGENTS_DIR).should be_true
		File.file?("#{AGENTS_DIR}/PassengerWatchdog").should be_true
		File.executable?("#{AGENTS_DIR}/PassengerWatchdog").should be_true
	end

	specify "the Apache 2 module exists" do
		File.file?(APACHE2_MODULE_PATH).should be_true
	end

	describe "passenger-config" do
		it "passenger-config is in #{BINDIR}" do
			realpath(which("passenger-config")).should == "#{BINDIR}/passenger-config"
		end

		it "shows the path to locations.ini" do
			capture_output("passenger-config --root").should == LOCATIONS_INI
		end

		it "recognizes the runtime libraries as compiled" do
			system("passenger-config --compiled").should be_true
		end

		it "recognizes the install as natively packaged" do
			system("passenger-config --natively-packaged").should be_true
		end

		it "recognizes the install as coming from an official package" do
			system("passenger-config --installed-from-release-package").should be_true
		end

		it "recognizes the system's Apache" do
			output = capture_output("passenger-config --detect-apache2")
			output.gsub!(/.*Final autodetection results\n/m, '')
			output.scan(/\* Found Apache .*\!/).size.should == 1
			output.should include("apxs2          : #{APXS2}\n")
			output.should include("Main executable: #{APACHE2}\n")
			output.should include("Control command: #{APACHE2CTL}\n")
			output.should include("Config file    : #{APACHE_CONFIG_FILE}\n")
			output.should include("Error log file : #{APACHE_ERROR_LOG}\n")
			output.should include(%Q{
   To start, stop or restart this specific Apache version:
      #{APACHE2CTL} start
      #{APACHE2CTL} stop
      #{APACHE2CTL} restart})
			output.should include(%Q{
   To troubleshoot, please read the logs in this file:
      #{APACHE_ERROR_LOG}})
		end

		it "shows the directory to the runtime library headers" do
			capture_output("passenger-config --includedir").should == INCLUDEDIR
		end

		it "shows the directory to the Nginx addon" do
			capture_output("passenger-config --nginx-addon-dir").should == NGINX_ADDON_DIR
		end

		it "shows the Nginx runtime libraries" do
			libs = capture_output("passenger-config --nginx-libs").split(" ")
			libs.should_not be_empty
			libs.each do |lib|
				File.file?(lib).should be_true
			end
		end

		it "validates the install as working" do
			system("passenger-config validate-install >/dev/null 2>/dev/null")
			[0, Config::ValidateInstallCommand::WARN_EXIT_CODE].should include($?.exitstatus)
		end
	end

	describe "passenger-memory-stats" do
		it "is in #{SBINDIR}" do
			realpath(which("passenger-memory-stats")).should == "#{SBINDIR}/passenger-memory-stats"
		end

		it "works" do
			capture_output("passenger-memory-stats").should =~ /Passenger processes/
		end
	end

	describe "passenger-install-apache2-module" do
		it "is in #{BINDIR}" do
			realpath(which("passenger-install-apache2-module")).should == "#{BINDIR}/passenger-install-apache2-module"
		end

		if SUPPORTS_COMPILING_APACHE_MODULE
			it "is able to compile the Apache module and doesn't break passenger-install-nginx-module" do
				install_apache2_module
				install_nginx_module
			end
		else
			it "checks whether the Apache module is installed" do
				output = capture_output("passenger-install-apache2-module --auto 2>&1")
				output.should =~ /Apache module is correctly installed/
			end
		end
	end

	describe "passenger-install-nginx-module" do
		it "is in #{BINDIR}" do
			realpath(which("passenger-install-nginx-module")).should == "#{BINDIR}/passenger-install-nginx-module"
		end

		if SUPPORTS_COMPILING_APACHE_MODULE
			it "is able to compile Nginx and doesn't break passenger-install-apache2-module" do
				install_nginx_module
				install_apache2_module
			end
		else
			it "is able to compile Nginx" do
				install_nginx_module
			end
		end
	end

	describe "Passenger Standalone" do
		it "is in #{BINDIR}" do
			realpath(which("passenger")).should == "#{BINDIR}/passenger"
		end

		it "works" do
			Dir.mktmpdir do |dir|
				File.chmod(0755, dir)
				Dir.chdir(dir) do
					File.open("config.ru", "w") do |f|
						f.write(%Q{
							app = lambda do |env|
								[200, { "Content-Type" => "text/plain" }, ["ok"]]
							end
							run app
						})
					end
					Dir.mkdir("public")
					Dir.mkdir("tmp")
					sh("passenger start --no-compile-runtime -p 4000 -d >/dev/null")
					begin
						open("http://127.0.0.1:4000/") do |f|
							f.read.should == "ok"
						end
					ensure
						sh("passenger stop -p 4000")
					end
				end
			end
		end
	end
end

end # module PhusionPassenger

# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013 Phusion
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

# Ensure that the natively installed tools are in PATH.
ENV['PATH'] = "/usr/bin:#{ENV['PATH']}"
LOCATIONS_INI = ENV['LOCATIONS_INI']
abort "Please set the LOCATIONS_INI environment variable to the right locations.ini" if !LOCATIONS_INI

BINDIR = "/usr/bin"
SBINDIR = "/usr/sbin"
INCLUDEDIR = "/usr/share/passenger/include"
NGINX_ADDON_DIR = "/usr/share/passenger/ngx_http_passenger_module"
DOCDIR = "/usr/share/doc/ruby-passenger"
RESOURCESDIR = "/usr/share/passenger"
RUBY_EXTENSION_SOURCE_DIR = "/usr/share/passenger/ruby_extension_source"
AGENTS_DIR = "/usr/lib/passenger/agents"
APACHE2_MODULE_PATH = "/usr/lib/apache2/modules/mod_passenger.so"

describe "A natively packaged Phusion Passenger" do
	def capture_output(command)
		output = `#{command}`.strip
		if $?.exitstatus == 0
			return output
		else
			abort "Command #{command} exited with status #{$?.exitstatus}"
		end
	end

	def which(command)
		return capture_output("which #{command}")
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

	specify "passenger-install-nginx-module is in #{BINDIR}" do
		which("passenger-install-nginx-module").should == "#{BINDIR}/passenger-install-nginx-module"
	end

	specify "passenger-status is in #{SBINDIR}" do
		which("passenger-status").should == "#{SBINDIR}/passenger-status"
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

	specify "the resources directory exists" do
		File.directory?(RESOURCESDIR).should be_true
		File.file?("#{RESOURCESDIR}/helper-scripts/rack-loader.rb").should be_true
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
			which("passenger-config").should == "#{BINDIR}/passenger-config"
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
			output.scan(/\* Found Apache \(.*\)\!/).size.should == 1
			output.should include(%Q{
      apxs2          : /usr/sbin/apxs
      Main executable: /usr/sbin/apache2
      Control command: /usr/sbin/apache2ctl
      Config file    : /etc/apache2/apache2.conf
      Error log file : /var/log/apache2/error.log})
			output.should include(%Q{
   To start, stop or restart this specific Apache version:
      /usr/sbin/apache2ctl start
      /usr/sbin/apache2ctl stop
      /usr/sbin/apache2ctl restart})
			output.should include(%Q{
   To troubleshoot, please read the logs in this file:
      /var/log/apache2/error.log})
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
	end

	describe "passenger-memory-stats" do
		it "is in #{SBINDIR}" do
			which("passenger-memory-stats").should == "#{SBINDIR}/passenger-memory-stats"
		end

		it "works" do
			capture_output("passenger-memory-stats").should =~ /Passenger processes/
		end
	end

	describe "passenger-install-apache2-module" do
		it "is in #{BINDIR}" do
			which("passenger-install-apache2-module").should == "#{BINDIR}/passenger-install-apache2-module"
		end

		it "prints the configuration snippet and exits" do
			output = capture_output("passenger-install-apache2-module --auto")
			output.should =~ /Please edit your Apache configuration file/
			output.should_not include("Compiling and installing Apache 2 module")
			output.should_not include("rake apache2")
		end

		it "produces a correct configuration snippet" do
			output = capture_output("passenger-install-apache2-module --auto")
			output.should include("LoadModule passenger_module #{APACHE2_MODULE_PATH}")
			output.should include("PassengerRoot #{LOCATIONS_INI}")
		end
	end
end

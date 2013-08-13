source_root = File.expand_path("../..", File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
require 'phusion_passenger/platform_info/binary_compatibility'
require 'tmpdir'
require 'fileutils'
require 'webrick'
require 'thread'

ENV['PATH'] = "#{PhusionPassenger.bin_dir}:#{ENV['PATH']}"
# This environment variable changes Passenger Standalone's behavior,
# so ensure that it's not set.
ENV.delete('PASSENGER_DEBUG')

describe "Passenger Standalone" do
	let(:version) { PhusionPassenger::VERSION_STRING }
	let(:nginx_version) { PhusionPassenger::PREFERRED_NGINX_VERSION }
	let(:compat_id) { PhusionPassenger::PlatformInfo.cxx_binary_compatibility_id }

	def sh(*command)
		if !system(*command)
			abort "Command failed: #{command.join(' ')}"
		end
	end

	def capture_output(command)
		output = `#{command} 2>&1`.strip
		if $?.exitstatus == 0
			return output
		else
			abort "Command #{command} exited with status #{$?.exitstatus}; output:\n#{output}"
		end
	end

	def start_server(document_root)
		server = WEBrick::HTTPServer.new(:BindAddress => '127.0.0.1',
			:Port => 0,
			:DocumentRoot => document_root,
			:Logger => WEBrick::Log.new("/dev/null"),
			:AccessLog => [])
		Thread.new do
			Thread.current.abort_on_exception = true
			server.start
		end
		[server, "http://127.0.0.1:#{server.config[:Port]}"]
	end

	def create_tarball(filename, contents = nil)
		filename = File.expand_path(filename)
		Dir.mktmpdir("tarball-") do |tarball_dir|
			Dir.chdir(tarball_dir) do
				if block_given?
					yield
				else
					contents.each do |content_name|
						create_file(content_name)
					end
				end
				sh "tar", "-czf", filename, "."
			end
		end
	end

	def create_file(filename, contents = nil)
		File.open(filename, "wb") do |f|
			f.write(contents) if contents
		end
	end

	specify "invoking 'passenger' without an argument is equivalent to 'passenger help'" do
		output = capture_output("passenger")
		output.should == capture_output("passenger help")
	end

	specify "'passenger --help' is equivalent to 'passenger help'" do
		output = capture_output("passenger")
		output.should == capture_output("passenger help")
	end

	specify "'passenger --version' displays the version number" do
		output = capture_output("passenger --version")
		output.should include("version #{PhusionPassenger::VERSION_STRING}\n")
	end

	describe "start command" do
		SUPPORT_BINARIES_DOWNLOAD_MESSAGE = "Downloading Passenger support binaries for your platform, if available"
		NGINX_BINARY_DOWNLOAD_MESSAGE = "Downloading Nginx binary for your platform, if available"
		NGINX_SOURCE_DOWNLOAD_MESSAGE = "Downloading Nginx..."
		COMPILING_MESSAGE = "Installing Phusion Passenger Standalone"

		context "if the runtime is not installed" do
			before :each do
				@runtime_dir = Dir.mktmpdir
				@webroot = Dir.mktmpdir
				@server, @base_url = start_server(@webroot)

				Dir.mkdir("#{@webroot}/#{version}")
				Dir.chdir("#{@webroot}/#{version}") do
					create_tarball("nginx-#{nginx_version}-#{compat_id}.tar.gz") do
						create_file("nginx")
						File.chmod(0755, "nginx")
					end
					create_tarball("support-#{compat_id}.tar.gz") do
						FileUtils.mkdir_p("agents")
						FileUtils.mkdir_p("common/libpassenger_common/ApplicationPool2")
						create_file("agents/PassengerWatchdog")
						create_file("common/libboost_oxt.a")
						create_file("common/libpassenger_common/ApplicationPool2/Implementation.o")
						File.chmod(0755, "agents/PassengerWatchdog")
					end
				end

				create_file("#{PhusionPassenger.resources_dir}/release.txt")
			end

			after :each do
				@server.stop
				File.unlink("#{PhusionPassenger.resources_dir}/release.txt")
				FileUtils.remove_entry_secure(@webroot)
				FileUtils.remove_entry_secure(@runtime_dir)
			end

			context "when originally packaged" do
				it "downloads binaries from the Internet" do
					@output = capture_output("passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--runtime-check-only " +
						"--binaries-url-root '#{@base_url}'")
					@output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
					@output.should_not include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
					@output.should_not include(COMPILING_MESSAGE)
				end

				it "builds the runtime if downloading fails" do
					# Yes, we're testing the entire build system here.
					@output = capture_output("passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--runtime-check-only " +
						"--binaries-url-root '#{@base_url}/wrong'")
					@output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
					@output.should include(COMPILING_MESSAGE)
				end
			end

			context "when natively packaged" do
				before :each do
					sh "passenger-config --make-locations-ini > '#{@runtime_dir}/locations.ini'"
					ENV['PASSENGER_LOCATION_CONFIGURATION_FILE'] = "#{@runtime_dir}/locations.ini"
					create_file("#{PhusionPassenger.lib_dir}/nginx")
				end

				after :each do
					ENV.delete('PASSENGER_LOCATION_CONFIGURATION_FILE')
					File.unlink("#{PhusionPassenger.lib_dir}/nginx")
				end

				it "downloads only the Nginx binary from the Internet" do
					File.rename("#{@webroot}/#{version}/nginx-#{nginx_version}-#{compat_id}.tar.gz",
						"#{@webroot}/#{version}/nginx-0.0.1-#{compat_id}.tar.gz")
					@output = capture_output("passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--runtime-check-only " +
						"--binaries-url-root '#{@base_url}' " +
						"--nginx-version 0.0.1")
					@output.should_not include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
					@output.should_not include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
					@output.should_not include(COMPILING_MESSAGE)
				end

				it "builds only Nginx if downloading fails" do
					# Yes, we're testing the build system here.
					@output = capture_output("passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--runtime-check-only " +
						"--binaries-url-root '#{@base_url}' " +
						"--nginx-version 1.0.0")
					@output.should_not include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
					@output.should include(COMPILING_MESSAGE)
				end
			end
		end

		context "if the runtime is installed" do
			it "doesn't download the runtime from the Internet"
			it "doesn't build the runtime"
		end

		it "starts a server which serves the application"
		it "daemonizes if -d is given"
	end

	describe "stop command" do
		# TODO
	end

	describe "status command" do
		# TODO
	end

	describe "help command" do
		it "displays the available commands" do
			capture_output("passenger help").should include("Available commands:")
		end
	end
end

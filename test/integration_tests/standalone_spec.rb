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

	def write_file(filename, contents)
		File.open(filename, "wb") do |f|
			f.write(contents)
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
		context "if the runtime is not installed" do
			context "when originally packaged" do
				before :each do
					@runtime_dir = Dir.mktmpdir
					@webroot = Dir.mktmpdir
					@server, @base_url = start_server(@webroot)

					version = PhusionPassenger::VERSION_STRING
					nginx_version = PhusionPassenger::PREFERRED_NGINX_VERSION
					compat_id = PhusionPassenger::PlatformInfo.cxx_binary_compatibility_id
					Dir.mkdir("#{@webroot}/#{version}")

					Dir.chdir("#{@webroot}/#{version}") do
						write_file("nginx", "")
						File.chmod(0755, "nginx")
						sh "tar -czf nginx-#{nginx_version}-#{compat_id}.tar.gz nginx"
						File.unlink("nginx")

						FileUtils.mkdir_p("agents")
						FileUtils.mkdir_p("common/libpassenger_common/ApplicationPool2")
						write_file("agents/PassengerWatchdog", "")
						write_file("common/libboost_oxt.a", "")
						write_file("common/libpassenger_common/ApplicationPool2/Implementation.o", "")
						File.chmod(0755, "agents/PassengerWatchdog")
						sh "tar -czf support-#{compat_id}.tar.gz agents common"
						FileUtils.rm_rf("agents")
						FileUtils.rm_rf("common")
					end

					write_file("#{PhusionPassenger.resources_dir}/release.txt", "")
				end

				after :each do
					@server.stop
					File.unlink("#{PhusionPassenger.resources_dir}/release.txt")
					FileUtils.remove_entry_secure(@runtime_dir)
					FileUtils.remove_entry_secure(@webroot)
				end

				it "downloads binaries from the Internet" do
					@output = capture_output("passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--runtime-check-only " +
						"--binaries-url-root '#{@base_url}'")
					@output.should include("Downloading Passenger support binaries for your platform, if available")
					@output.should include("Downloading Nginx binary for your platform, if available")
					@output.should_not include("Downloading Nginx...")
					@output.should_not include("Installing Phusion Passenger Standalone")
				end

				it "builds the runtime if downloading fails" do
					@output = capture_output("passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--runtime-check-only " +
						"--binaries-url-root '#{@base_url}/wrong'")
					@output.should include("Downloading Passenger support binaries for your platform, if available")
					@output.should include("Downloading Nginx binary for your platform, if available")
					@output.should include("Downloading Nginx...")
					@output.should include("Installing Phusion Passenger Standalone")
				end
			end

			context "when natively packaged" do
				it "downloads only the Nginx binary from the Internet"
				it "builds only Nginx if downloading fails"
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

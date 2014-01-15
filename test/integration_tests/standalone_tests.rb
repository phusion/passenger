source_root = File.expand_path("../..", File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
require 'tmpdir'
require 'fileutils'
require 'webrick'
require 'thread'
require 'open-uri'

ENV['PATH'] = "#{PhusionPassenger.bin_dir}:#{ENV['PATH']}"
# This environment variable changes Passenger Standalone's behavior,
# so ensure that it's not set.
ENV.delete('PASSENGER_DEBUG')
ENV['PASSENGER_DOWNLOAD_NATIVE_SUPPORT_BINARY'] = '0'
ENV['PASSENGER_COMPILE_NATIVE_SUPPORT_BINARY']  = '0'

module PhusionPassenger

describe "Passenger Standalone" do
	after :each do
		ENV.delete('PASSENGER_DEBUG')
	end

	let(:version) { PhusionPassenger::VERSION_STRING }
	let(:nginx_version) { PhusionPassenger::PREFERRED_NGINX_VERSION }
	let(:compat_id) { PhusionPassenger::PlatformInfo.cxx_binary_compatibility_id }

	def use_binaries_from_source_root!
		ENV['PASSENGER_DEBUG'] = '1'
	end

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

	def create_dummy_support_binaries
		Dir.mkdir("agents") if !File.exist?("agents")
		["PassengerWatchdog", "PassengerHelperAgent", "PassengerLoggingAgent"].each do |exe|
			File.open("agents/#{exe}", "w") do |f|
				f.puts "#!/bin/bash"
				f.puts "echo PASS"
			end
			File.chmod(0755, "agents/#{exe}")
		end
	end

	def create_dummy_nginx_binary
		File.open("PassengerWebHelper", "w") do |f|
			f.puts "#!/bin/bash"
			f.puts "echo nginx version: 1.0.0"
		end
		File.chmod(0755, "PassengerWebHelper")
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
		SUPPORT_BINARIES_DOWNLOAD_MESSAGE = " --> Downloading #{PROGRAM_NAME} support binaries for your platform"
		NGINX_BINARY_DOWNLOAD_MESSAGE = "Downloading web helper for your platform"
		NGINX_SOURCE_DOWNLOAD_MESSAGE = "Downloading web helper source code..."
		COMPILING_MESSAGE = "Installing #{PROGRAM_NAME} Standalone..."

		def test_serving_application(passenger_command)
			Dir.chdir(@runtime_dir) do
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
				sh("#{passenger_command} -p 4000 -d >/dev/null")
				begin
					open("http://127.0.0.1:4000/") do |f|
						f.read.should == "ok"
					end
				ensure
					sh("passenger stop -p 4000")
				end
			end
		end

		context "if the runtime is not installed" do
			before :each do
				@runtime_dir = Dir.mktmpdir
				@webroot = Dir.mktmpdir
				@server, @base_url = start_server(@webroot)

				Dir.mkdir("#{@webroot}/#{version}")
				Dir.chdir("#{@webroot}/#{version}") do
					create_tarball("webhelper-#{nginx_version}-#{compat_id}.tar.gz") do
						create_dummy_nginx_binary
					end
					create_tarball("support-#{compat_id}.tar.gz") do
						FileUtils.mkdir_p("agents")
						FileUtils.mkdir_p("common/libpassenger_common/ApplicationPool2")
						create_file("common/libboost_oxt.a")
						create_file("common/libpassenger_common/ApplicationPool2/Implementation.o")
						create_dummy_support_binaries
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
					command = "passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--binaries-url-root '#{@base_url}/wrong'"
					@output = capture_output("#{command} --runtime-check-only")
					@output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
					@output.should include(COMPILING_MESSAGE)

					test_serving_application(command)
				end

				specify "if the downloaded support binaries work but the downloaded web helper binary doesn't, " +
					"and web helper compilation doesn't succeed the first time, then web helper compilation " +
					"succeeds the second time" do
					Dir.chdir("#{@webroot}/#{version}") do
						create_tarball("support-#{compat_id}.tar.gz") do
							FileUtils.cp_r(Dir["#{PhusionPassenger.source_root}/buildout/*"],
								".")
						end
						create_tarball("webhelper-#{nginx_version}-#{compat_id}.tar.gz") do
							create_file("PassengerWebHelper",
								"#!/bin/sh\n" +
								"exit 1\n")
						end
					end

					# Temporarily make Passenger Standalone think our runtime is
					# not compiled.
					File.rename("#{PhusionPassenger.source_root}/buildout",
						"#{PhusionPassenger.source_root}/buildout.renamed")
					begin
						command = "passenger start " +
							"--runtime-dir '#{@runtime_dir}' " +
							"--binaries-url-root '#{@base_url}'"
						
						@output = `#{command} --runtime-check-only --no-compile-runtime 2>&1`
						$?.exitstatus.should_not == 0
						@output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
						@output.should include("All good\n")
						@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
						@output.should include("Not usable, will compile from source")
						@output.should include("Refusing to compile the Phusion Passenger Standalone runtime")

						@output = capture_output("#{command} --runtime-check-only")
						@output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
						@output.should include(COMPILING_MESSAGE)
						File.exist?("#{PhusionPassenger.source_root}/buildout").should be_false
						
						test_serving_application("#{command} --no-compile-runtime")
						File.exist?("#{PhusionPassenger.source_root}/buildout").should be_false
					ensure
						FileUtils.rm_rf("#{PhusionPassenger.source_root}/buildout")
						File.rename("#{PhusionPassenger.source_root}/buildout.renamed",
							"#{PhusionPassenger.source_root}/buildout")
					end
				end

				it "starts a server which serves the application" do
					# The last test already tests this. This empty test here
					# is merely to show the intent of the tests, and to
					# speed up the test suite by preventing an unnecessary
					# compilation.
				end
			end

			context "when natively packaged" do
				before :each do
					sh "passenger-config --make-locations-ini --for-native-packaging-method=deb " +
						"> '#{@runtime_dir}/locations.ini'"
					ENV['PASSENGER_LOCATION_CONFIGURATION_FILE'] = "#{@runtime_dir}/locations.ini"
					create_file("#{PhusionPassenger.lib_dir}/PassengerWebHelper")
				end

				after :each do
					ENV.delete('PASSENGER_LOCATION_CONFIGURATION_FILE')
					File.unlink("#{PhusionPassenger.lib_dir}/PassengerWebHelper")
				end

				it "downloads only the Nginx binary from the Internet" do
					File.rename("#{@webroot}/#{version}/webhelper-#{nginx_version}-#{compat_id}.tar.gz",
						"#{@webroot}/#{version}/webhelper-0.0.1-#{compat_id}.tar.gz")
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

				it "only builds Nginx if downloading fails" do
					# Yes, we're testing the build system here.
					command = "passenger start " +
						"--runtime-dir '#{@runtime_dir}' " +
						"--binaries-url-root '#{@base_url}' " +
						"--nginx-version 1.4.1"
					@output = capture_output("#{command} --runtime-check-only")
					@output.should_not include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
					@output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
					@output.should include(COMPILING_MESSAGE)

					test_serving_application(command)
				end

				it "starts a server which serves the application" do
					# The last test already tests this. This empty test here
					# is merely to show the intent of the tests, and to
					# speed up the test suite by preventing an unnecessary
					# compilation.
				end
			end
		end

		context "if the runtime is installed" do
			it "doesn't download the runtime from the Internet"
			it "doesn't build the runtime"
		end

		it "daemonizes if -d is given" do
			# Earlier tests already test this. This empty test here
			# is merely to show the intent of the tests, and to
			# speed up the test suite by preventing an unnecessary
			# compilation.
		end
	end

	describe "help command" do
		it "displays the available commands" do
			capture_output("passenger help").should include("Available commands:")
		end
	end
end

end # module PhusionPassenger

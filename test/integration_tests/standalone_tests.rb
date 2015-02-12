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

  def create_dummy_support_binaries
    Dir.mkdir("support-binaries") if !File.exist?("support-binaries")
    File.open("support-binaries/#{AGENT_EXE}", "w") do |f|
      f.puts "#!/bin/bash"
      f.puts "echo PASS"
    end
    File.chmod(0755, "support-binaries/#{AGENT_EXE}")
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
    AGENT_BINARY_DOWNLOAD_MESSAGE = "--> Downloading a #{PROGRAM_NAME} agent binary for your platform"
    AGENT_BINARY_COMPILE_MESSAGE  = "Compiling #{PROGRAM_NAME} agent"
    NGINX_BINARY_INSTALL_MESSAGE  = "Installing Nginx"

    def test_serving_application(passenger_command)
      Dir.mktmpdir do |tmpdir|
        Dir.chdir(tmpdir) do
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
          sh("#{passenger_command} -p 4000 -d --disable-turbocaching >/dev/null")
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

    context "if the runtime is not installed" do
      before :each do
        @user_dir = File.expand_path("~/#{USER_NAMESPACE_DIRNAME}")
        if File.exist?("buildout.old")
          raise "buildout.old exists. Please fix this first."
        end
        if File.exist?("#{@user_dir}.old")
          raise "#{@user_dir} exists. Please fix this first."
        end
        if PhusionPassenger.build_system_dir
          FileUtils.mv("#{PhusionPassenger.build_system_dir}/buildout",
            "#{PhusionPassenger.build_system_dir}/buildout.old")
        end
        if File.exist?(@user_dir)
          FileUtils.mv(@user_dir, "#{@user_dir}.old")
        end
      end

      after :each do
        FileUtils.rm_rf("#{PhusionPassenger.build_system_dir}/buildout")
        FileUtils.rm_rf(@user_dir)
        if PhusionPassenger.build_system_dir
          FileUtils.mv("#{PhusionPassenger.build_system_dir}/buildout.old",
            "#{PhusionPassenger.build_system_dir}/buildout")
        end
        if File.exist?("#{@user_dir}.old")
          FileUtils.mv("#{@user_dir}.old", @user_dir)
        end
      end

      context "when natively packaged" do
        it "tries to install the runtime" do
          command = "passenger start --no-install-runtime --runtime-check-only"
          `#{command} 2>&1`.should include("Refusing to install")
        end

        it "starts a server which serves the application" do
          output = capture_output("passenger start --runtime-check-only")
          output.should include(AGENT_BINARY_DOWNLOAD_MESSAGE)
          output.should include(NGINX_BINARY_INSTALL_MESSAGE)
          test_serving_application("passenger start")
        end
      end

      context "when custom packaged" do
        before :each do
          @tmpdir = Dir.mktmpdir
          sh "passenger-config --make-locations-ini --for-packaging-method=deb " +
            "> '#{@tmpdir}/locations.ini'"
          ENV['PASSENGER_LOCATION_CONFIGURATION_FILE'] = "#{@tmpdir}/locations.ini"
        end

        after :each do
          ENV.delete('PASSENGER_LOCATION_CONFIGURATION_FILE')
          FileUtils.remove_entry_secure(@tmpdir)
        end

        it "tries to install the runtime" do
          command = "passenger start --no-install-runtime --runtime-check-only"
          `#{command} 2>&1`.should include("Refusing to install")
        end

        it "starts a server which serves the application" do
          output = capture_output("passenger start --runtime-check-only")
          output.should include(AGENT_BINARY_DOWNLOAD_MESSAGE)
          output.should include(NGINX_BINARY_INSTALL_MESSAGE)
          test_serving_application("passenger start")
        end
      end

      # TODO: move these tests to config/install_standalone_runtime_command_spec.rb

      # before :each do
      #   @runtime_dir = Dir.mktmpdir
      #   @webroot = Dir.mktmpdir
      #   @server, @base_url = start_server(@webroot)

      #   Dir.mkdir("#{@webroot}/#{version}")
      #   Dir.chdir("#{@webroot}/#{version}") do
      #     create_tarball("webhelper-#{nginx_version}-#{compat_id}.tar.gz") do
      #       create_dummy_nginx_binary
      #     end
      #     create_tarball("support-#{compat_id}.tar.gz") do
      #       FileUtils.mkdir_p("support-binaries")
      #       FileUtils.mkdir_p("common/libpassenger_common/ApplicationPool2")
      #       create_file("common/libboost_oxt.a")
      #       create_file("common/libpassenger_common/ApplicationPool2/Implementation.o")
      #       create_dummy_support_binaries
      #     end
      #   end

      #   create_file("#{PhusionPassenger.resources_dir}/release.txt")
      # end

      # after :each do
      #   @server.stop
      #   File.unlink("#{PhusionPassenger.resources_dir}/release.txt")
      #   FileUtils.remove_entry_secure(@webroot)
      #   FileUtils.remove_entry_secure(@runtime_dir)
      # end

      # context "when originally packaged" do
      #   it "downloads binaries from the Internet" do
      #     @output = capture_output("passenger start " +
      #       "--runtime-dir '#{@runtime_dir}' " +
      #       "--runtime-check-only " +
      #       "--binaries-url-root '#{@base_url}'")
      #     @output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
      #     @output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
      #     @output.should_not include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
      #     @output.should_not include(COMPILING_MESSAGE)
      #   end

      #   it "builds the runtime if downloading fails" do
      #     # Yes, we're testing the entire build system here.
      #     command = "passenger start " +
      #       "--runtime-dir '#{@runtime_dir}' " +
      #       "--binaries-url-root '#{@base_url}/wrong'"
      #     @output = capture_output("#{command} --runtime-check-only")
      #     @output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
      #     @output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
      #     @output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
      #     @output.should include(COMPILING_MESSAGE)

      #     test_serving_application(command)
      #   end

      #   specify "if the downloaded support binaries work but the downloaded web helper binary doesn't, " +
      #     "and web helper compilation doesn't succeed the first time, then web helper compilation " +
      #     "succeeds the second time" do
      #     Dir.chdir("#{@webroot}/#{version}") do
      #       create_tarball("support-#{compat_id}.tar.gz") do
      #         FileUtils.cp_r(Dir["#{PhusionPassenger.build_system_dir}/buildout/*"],
      #           ".")
      #       end
      #       create_tarball("webhelper-#{nginx_version}-#{compat_id}.tar.gz") do
      #         create_file("PassengerWebHelper",
      #           "#!/bin/sh\n" +
      #           "exit 1\n")
      #       end
      #     end

      #     # Temporarily make Passenger Standalone think our runtime is
      #     # not compiled.
      #     File.rename("#{PhusionPassenger.build_system_dir}/buildout",
      #       "#{PhusionPassenger.build_system_dir}/buildout.renamed")
      #     begin
      #       command = "passenger start " +
      #         "--runtime-dir '#{@runtime_dir}' " +
      #         "--binaries-url-root '#{@base_url}'"

      #       @output = `#{command} --runtime-check-only --no-compile-runtime 2>&1`
      #       $?.exitstatus.should_not == 0
      #       @output.should include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
      #       @output.should include("All good\n")
      #       @output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
      #       @output.should include("Not usable, will compile from source")
      #       @output.should include("Refusing to compile the Phusion Passenger Standalone runtime")

      #       @output = capture_output("#{command} --runtime-check-only")
      #       @output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
      #       @output.should include(COMPILING_MESSAGE)
      #       File.exist?("#{PhusionPassenger.build_system_dir}/buildout").should be_false

      #       test_serving_application("#{command} --no-compile-runtime")
      #       File.exist?("#{PhusionPassenger.build_system_dir}/buildout").should be_false
      #     ensure
      #       FileUtils.rm_rf("#{PhusionPassenger.build_system_dir}/buildout")
      #       File.rename("#{PhusionPassenger.build_system_dir}/buildout.renamed",
      #         "#{PhusionPassenger.build_system_dir}/buildout")
      #     end
      #   end
      # end

      # context "when custom packaged" do
      #   before :each do
      #     sh "passenger-config --make-locations-ini --for-packaging-method=deb " +
      #       "> '#{@runtime_dir}/locations.ini'"
      #     ENV['PASSENGER_LOCATION_CONFIGURATION_FILE'] = "#{@runtime_dir}/locations.ini"
      #     create_file("#{PhusionPassenger.lib_dir}/PassengerWebHelper")
      #   end

      #   after :each do
      #     ENV.delete('PASSENGER_LOCATION_CONFIGURATION_FILE')
      #     File.unlink("#{PhusionPassenger.lib_dir}/PassengerWebHelper")
      #   end

      #   it "downloads only the Nginx binary from the Internet" do
      #     File.rename("#{@webroot}/#{version}/webhelper-#{nginx_version}-#{compat_id}.tar.gz",
      #       "#{@webroot}/#{version}/webhelper-0.0.1-#{compat_id}.tar.gz")
      #     @output = capture_output("passenger start " +
      #       "--runtime-dir '#{@runtime_dir}' " +
      #       "--runtime-check-only " +
      #       "--binaries-url-root '#{@base_url}' " +
      #       "--nginx-version 0.0.1")
      #     @output.should_not include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
      #     @output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
      #     @output.should_not include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
      #     @output.should_not include(COMPILING_MESSAGE)
      #   end

      #   it "only builds Nginx if downloading fails" do
      #     # Yes, we're testing the build system here.
      #     command = "passenger start " +
      #       "--runtime-dir '#{@runtime_dir}' " +
      #       "--binaries-url-root '#{@base_url}' " +
      #       "--nginx-version 1.4.1"
      #     @output = capture_output("#{command} --runtime-check-only")
      #     @output.should_not include(SUPPORT_BINARIES_DOWNLOAD_MESSAGE)
      #     @output.should include(NGINX_BINARY_DOWNLOAD_MESSAGE)
      #     @output.should include(NGINX_SOURCE_DOWNLOAD_MESSAGE)
      #     @output.should include(COMPILING_MESSAGE)

      #     test_serving_application(command)
      #   end
      # end
    end

    context "if the runtime is installed" do
      before :all do
        capture_output("passenger-config compile-nginx-engine")
      end

      it "doesn't download the runtime from the Internet" do
        command = "passenger start --no-install-runtime --runtime-check-only"
        capture_output(command).should_not include(AGENT_BINARY_DOWNLOAD_MESSAGE)
      end

      it "doesn't build the runtime" do
        command = "passenger start --no-install-runtime --runtime-check-only"
        capture_output(command).should_not include(AGENT_BINARY_COMPILE_MESSAGE)
      end

      it "starts a server which serves the application" do
        test_serving_application("passenger start")
      end
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

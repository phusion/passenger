require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'standalone/runtime_installer'
require 'tmpdir'
require 'fileutils'
require 'stringio'

module PhusionPassenger
module Standalone

describe RuntimeInstaller do
	before :each do
		@temp_dir = Dir.mktmpdir
		Dir.mkdir("#{@temp_dir}/support")
		Dir.mkdir("#{@temp_dir}/nginx")
		@logs = StringIO.new
		PhusionPassenger.stub(:installed_from_release_package?).and_return(true)
	end

	after :each do
		FileUtils.remove_entry_secure(@temp_dir)
	end

	let(:binaries_url_root) { "http://somewhere" }
	let(:version) { PhusionPassenger::VERSION_STRING }
	let(:nginx_version) { PhusionPassenger::PREFERRED_NGINX_VERSION }
	let(:cxx_compat_id) { PlatformInfo.cxx_binary_compatibility_id }
	let(:support_binaries_url) { "#{binaries_url_root}/#{version}/support-#{cxx_compat_id}.tar.gz" }
	let(:nginx_binary_url) { "#{binaries_url_root}/#{version}/webhelper-#{nginx_version}-#{cxx_compat_id}.tar.gz" }
	let(:nginx_source_url) { "http://nginx.org/download/nginx-#{nginx_version}.tar.gz" }

	def create_installer(options = {})
		options = {
			:binaries_url_root => binaries_url_root,
			:stdout => @logs,
			:stderr => @logs
		}.merge(options)
		@installer = RuntimeInstaller.new(options)
	end

	def create_tarball(filename, contents = nil)
		Dir.mktmpdir("tarball-", @temp_dir) do |tarball_dir|
			Dir.chdir(tarball_dir) do
				if block_given?
					yield
				else
					contents.each do |content_name|
						File.open(content_name, "w").close
					end
				end
				sh "tar", "-czf", filename, "."
			end
		end
	end

	def create_dummy_support_binaries
		Dir.mkdir("agents")
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

	def create_dummy_nginx_source
		Dir.mkdir("nginx-#{nginx_version}")
		File.open("nginx-#{nginx_version}/configure", "w") do |f|
			f.puts %Q{echo "$@" > '#{@temp_dir}/configure.txt'}
		end
		File.chmod(0700, "nginx-#{nginx_version}/configure")
		File.open("nginx-#{nginx_version}/Makefile", "w") do |f|
			f.puts("all:")
			f.puts("	mkdir objs")
			f.puts("	echo ok > objs/nginx")
		end
	end

	def create_file(filename)
		File.open(filename, "w").close
	end

	def sh(*command)
		if !system(*command)
			raise "Command failed: #{command.join(' ')}"
		end
	end

	def test_download_nginx_binary
		create_installer(:targets => [:nginx],
				:nginx_dir => "#{@temp_dir}/nginx",
				:lib_dir => PhusionPassenger.lib_dir)

		@installer.should_receive(:download).
			and_return do |url, output, options|
				url.should == nginx_binary_url
				options[:use_cache].should be_true
				create_tarball(output) do
					create_dummy_nginx_binary
				end
				true
			end

		@installer.should_receive(:check_for_download_tool)
		@installer.should_not_receive(:check_depdendencies)
		@installer.should_not_receive(:compile_support_binaries)
		@installer.should_not_receive(:download_and_extract_nginx_sources)
		@installer.should_not_receive(:compile_nginx)
		@installer.run

		File.exist?("#{@temp_dir}/nginx/PassengerWebHelper").should be_true
	end

	def test_building_nginx_binary
		create_installer(:targets => [:nginx],
				:nginx_dir => "#{@temp_dir}/nginx",
				:lib_dir   => PhusionPassenger.lib_dir)

		@installer.should_receive(:download).twice.and_return do |url, output|
			if url == nginx_binary_url
				false
			elsif url == nginx_source_url
				create_tarball(output) do
					create_dummy_nginx_source
				end
				true
			else
				raise "Unexpected download URL: #{url}"
			end
		end

		@installer.should_receive(:check_for_download_tool)
		@installer.should_receive(:check_dependencies).and_return(true)
		@installer.should_not_receive(:compile_support_binaries)
		@installer.should_receive(:strip_binary).
			with(an_instance_of(String)).
			and_return(true)
		@installer.run

		File.read("#{@temp_dir}/nginx/PassengerWebHelper").should == "ok\n"
		File.read("#{@temp_dir}/configure.txt").should include(
			"--add-module=#{PhusionPassenger.nginx_module_source_dir}")
	end

	context "when originally packaged" do
		before :each do
			PhusionPassenger.stub(:originally_packaged?).and_return(true)
			PhusionPassenger.stub(:natively_packaged?).and_return(false)
		end

		it "downloads the support binaries from the Internet if :support_binaries is specified as target" do
			create_installer(:targets => [:support_binaries],
				:support_dir => "#{@temp_dir}/support")

			@installer.should_receive(:download).
				and_return do |url, output, options|
					url.should == "#{binaries_url_root}/#{version}/support-#{cxx_compat_id}.tar.gz"
					options[:use_cache].should be_true
					create_tarball(output) do
						create_dummy_support_binaries
					end
					true
				end
			
			@installer.should_receive(:check_for_download_tool)
			@installer.should_not_receive(:check_depdendencies)
			@installer.should_not_receive(:compile_support_binaries)
			@installer.should_not_receive(:download_and_extract_nginx_sources)
			@installer.should_not_receive(:compile_nginx)
			@installer.run

			File.exist?("#{@temp_dir}/support/agents/PassengerWatchdog").should be_true
		end

		it "downloads the Nginx binary from the Internet if :nginx is specified as target" do
			test_download_nginx_binary
		end

		it "downloads everything if :support_binaries and :nginx are both specified as target" do
			create_installer(:targets => [:support_binaries, :nginx],
				:support_dir => "#{@temp_dir}/support",
				:nginx_dir => "#{@temp_dir}/nginx",
				:lib_dir => PhusionPassenger.lib_dir)

			@installer.should_receive(:download).
				twice.
				and_return do |url, output, options|
					if url == support_binaries_url
						create_tarball(output) do
							create_dummy_support_binaries
						end
					elsif url == nginx_binary_url
						create_tarball(output) do
							create_dummy_nginx_binary
						end
					else
						raise "Unexpected download URL: #{url}"
					end
					options[:use_cache].should be_true
					true
				end

			@installer.should_receive(:check_for_download_tool)
			@installer.should_not_receive(:check_depdendencies)
			@installer.should_not_receive(:compile_support_binaries)
			@installer.should_not_receive(:download_and_extract_nginx_sources)
			@installer.should_not_receive(:compile_nginx)
			@installer.run

			File.exist?("#{@temp_dir}/support/agents/PassengerWatchdog").should be_true
			File.exist?("#{@temp_dir}/nginx/PassengerWebHelper").should be_true
		end

		it "builds the support binaries if it cannot be downloaded" do
			create_installer(:targets => [:support_binaries],
				:support_dir => "#{@temp_dir}/support")
			nginx_libs = COMMON_LIBRARY.
				only(*NGINX_LIBS_SELECTOR).
				set_output_dir("#{@temp_dir}/support/libpassenger_common").
				link_objects
			built_files = nil

			@installer.should_receive(:run_rake_task!).with(
				"nginx_without_native_support CACHING=false OUTPUT_DIR='#{@temp_dir}/support'").
				and_return do
					FileUtils.mkdir_p("#{@temp_dir}/agents")
					create_file("#{@temp_dir}/agents/PassengerWatchdog")

					nginx_libs.each do |object_filename|
						dir = File.dirname(object_filename)
						FileUtils.mkdir_p(dir)
						create_file(object_filename)
					end

					built_files = `find '#{@temp_dir}/support'`
				end

			@installer.should_receive(:check_for_download_tool)
			@installer.should_receive(:download).and_return(false)
			@installer.should_receive(:check_dependencies).and_return(true)
			@installer.should_not_receive(:download_and_extract_nginx_sources)
			@installer.should_not_receive(:compile_nginx)
			@installer.run
			`find '#{@temp_dir}/support'`.should == built_files
		end

		it "builds the Nginx binary if it cannot be downloaded" do
			test_building_nginx_binary
		end

		it "aborts if the support binaries cannot be built" do
			create_installer(:targets => [:support_binaries],
				:support_dir => "#{@temp_dir}/support")

			@installer.should_receive(:run_rake_task!).with(
				"nginx_without_native_support CACHING=false OUTPUT_DIR='#{@temp_dir}/support'").
				and_raise(RuntimeError, "Rake failed")

			@installer.should_receive(:check_for_download_tool)
			@installer.should_receive(:download).and_return(false)
			@installer.should_receive(:check_dependencies).and_return(true)
			@installer.should_not_receive(:download_and_extract_nginx_sources)
			@installer.should_not_receive(:compile_nginx)
			lambda { @installer.run }.should raise_error(SystemExit)
			@logs.string.should include("Rake failed")
		end
	end

	context "when natively packaged" do
		before :each do
			PhusionPassenger.stub(:source_root).and_return("/locations.ini")
			PhusionPassenger.stub(:originally_packaged?).and_return(false)
			PhusionPassenger.stub(:natively_packaged?).and_return(true)
		end

		it "refuses to accept :support_binaries as target" do
			block = lambda do
				create_installer(:targets => [:support_binaries],
					:support_dir => "#{@temp_dir}/support")
			end
			block.should raise_error(ArgumentError, /You cannot specify :support_binaries/)
		end

		it "downloads the Nginx binary from the Internet if :nginx is specified as target" do
			test_download_nginx_binary
		end

		it "builds the Nginx binary if it cannot be downloaded" do
			test_building_nginx_binary
		end
	end

	it "commits downloaded binaries after checking whether they're usable" do
		create_installer(:targets => [:support_binaries, :nginx],
				:support_dir => "#{@temp_dir}/support",
				:nginx_dir => "#{@temp_dir}/nginx",
				:lib_dir => PhusionPassenger.lib_dir)

		@installer.should_receive(:download).
			exactly(3).times.
			and_return do |url, output, options|
				if url == support_binaries_url
					options[:use_cache].should be_true
					create_tarball(output) do
						create_dummy_support_binaries
					end
				elsif url == nginx_binary_url
					options[:use_cache].should be_true
					create_tarball(output) do
						create_dummy_nginx_binary
					end
				elsif url == nginx_source_url
					create_tarball(output) do
						create_dummy_nginx_source
					end
				else
					raise "Unexpected download URL: #{url}"
				end
				true
			end

		@installer.should_receive(:check_for_download_tool)
		@installer.should_receive(:check_support_binaries).and_return(false)
		@installer.should_receive(:check_nginx_binary).and_return(false)
		@installer.should_receive(:check_dependencies).and_return(true)
		@installer.should_receive(:compile_support_binaries)
		@installer.should_receive(:compile_nginx)
		@installer.run

		Dir["#{@temp_dir}/nginx/*"].should be_empty
		Dir["#{@temp_dir}/support/*"].should be_empty
	end

	it "aborts if the Nginx source tarball cannot be extracted" do
		create_installer(:targets => [:nginx],
			:nginx_dir => "#{@temp_dir}/nginx",
			:lib_dir   => PhusionPassenger.lib_dir)

		@installer.should_receive(:download).twice.and_return do |url, output, options|
			if url == nginx_binary_url
				false
			elsif url == nginx_source_url
				File.open(output, "w") do |f|
					f.write("garbage")
				end
				true
			else
				raise "Unexpected download URL: #{url}"
			end
		end

		@installer.should_receive(:check_for_download_tool)
		@installer.should_receive(:check_dependencies).and_return(true)
		@installer.should_not_receive(:compile_support_binaries)
		lambda { @installer.run }.should raise_error(SystemExit)
		@logs.string.should =~ %r{Unable to download or extract Nginx source tarball}
	end
	
	it "aborts if the Nginx binary cannot be built" do
		create_installer(:targets => [:nginx],
			:nginx_dir => "#{@temp_dir}/nginx",
			:lib_dir   => PhusionPassenger.lib_dir)

		@installer.should_receive(:download).twice.and_return do |url, output, options|
			if url == nginx_binary_url
				false
			elsif url == nginx_source_url
				create_tarball(output) do
					Dir.mkdir("nginx-#{nginx_version}")
					File.open("nginx-#{nginx_version}/configure", "w") do |f|
						f.puts("#!/bin/bash")
						f.puts("echo error")
						f.puts("exit 1")
					end
					File.chmod(0700, "nginx-#{nginx_version}/configure")
				end
				true
			else
				raise "Unexpected download URL: #{url}"
			end
		end

		@installer.should_receive(:check_for_download_tool)
		@installer.should_receive(:check_dependencies).and_return(true)
		@installer.should_not_receive(:compile_support_binaries)
		lambda { @installer.run }.should raise_error(SystemExit)
		@logs.string.should =~ %r{command failed:.*./configure}
	end
end

end # module Standalone
end # module PhusionPassenger

source_root = File.expand_path("../..", File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
require 'tmpdir'
require 'fileutils'
require 'open-uri'

ENV['PATH'] = "#{PhusionPassenger.bin_dir}:#{ENV['PATH']}"
# This environment variable changes Passenger Standalone's behavior,
# so ensure that it's not set.
ENV.delete('PASSENGER_DEBUG')

describe "Downloaded Phusion Passenger binaries" do
	before :each do
		@temp_dir = Dir.mktmpdir
		File.open("#{PhusionPassenger.resources_dir}/release.txt", "w").close
	end

	after :each do
		FileUtils.remove_entry_secure(@temp_dir)
		File.unlink("#{PhusionPassenger.resources_dir}/release.txt")
	end

	let(:version) { PhusionPassenger::VERSION_STRING }
	let(:nginx_version) { PhusionPassenger::PREFERRED_NGINX_VERSION }
	let(:compat_id) { PhusionPassenger::PlatformInfo.cxx_binary_compatibility_id }

	def sh(*command)
		if !system(*command)
			abort "Command failed: #{command.join(' ')}"
		end
	end

	it "works" do
		Dir.mkdir("#{@temp_dir}/#{version}")
		Dir.chdir("#{@temp_dir}/#{version}") do
			tarballs = Dir["#{PhusionPassenger.download_cache_dir}/*.tar.gz"]
			tarballs.should_not be_empty
			
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
			Dir.mkdir("log")

			begin
				sh("passenger start " +
					"-p 4000 " +
					"-d " +
					"--no-compile-runtime " +
					"--binaries-url-root http://127.0.0.1:4001 " +
					"--runtime-dir '#{@temp_dir}' >log/start.log")
			rescue Exception
				system("cat log/start.log")
				raise
			end
			begin
				open("http://127.0.0.1:4000/") do |f|
					f.read.should == "ok"
				end
			rescue
				system("cat log/passenger.4000.log")
				raise
			ensure
				sh "passenger stop -p 4000"
			end
		end
	end
end
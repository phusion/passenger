require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'standalone/runtime_locator'
require 'tmpdir'
require 'fileutils'
require 'json'

module PhusionPassenger
module Standalone

describe RuntimeLocator do
	before :each do
		@temp_dir = Dir.mktmpdir
		@locator = RuntimeLocator.new(@temp_dir)
	end

	after :each do
		FileUtils.remove_entry_secure(@temp_dir)
	end

	def create_file(filename, contents = nil)
		File.open(filename, "w") do |f|
			f.write(contents) if contents
		end
	end

	def create_nginx(nginx_version)
		version = PhusionPassenger::VERSION_STRING
		cxx_compat_id = PlatformInfo.cxx_binary_compatibility_id
		nginx_dir = "#{@temp_dir}/#{version}/webhelper-#{nginx_version}-#{cxx_compat_id}"
		FileUtils.mkdir_p(nginx_dir)
		@nginx_filename = "#{nginx_dir}/PassengerWebHelper"
		create_file(@nginx_filename)
		File.chmod(0755, @nginx_filename)
	end

	context "when originally packaged" do
		before :each do
			PhusionPassenger.stub(:originally_packaged?).and_return(true)
			PhusionPassenger.stub(:natively_packaged?).and_return(false)
		end

		context "if PASSENGER_DEBUG is set" do
			before :each do
				ENV['PASSENGER_DEBUG'] = '1'
			end

			after :each do
				ENV.delete('PASSENGER_DEBUG')
			end

			it "returns SOURCE_ROOT/buildout as the support directory" do
				@locator.find_support_dir.should == "#{PhusionPassenger.source_root}/buildout"
			end
		end

		context "if no PASSENGER_DEBUG is set" do
			context "if there is a support directory in the home directory" do
				before :each do
					version = PhusionPassenger::VERSION_STRING
					cxx_compat_id = PlatformInfo.cxx_binary_compatibility_id
					@support_dir = "#{@temp_dir}/#{version}/support-#{cxx_compat_id}"
					FileUtils.mkdir_p("#{@support_dir}/agents")
					FileUtils.mkdir_p("#{@support_dir}/common/libpassenger_common/ApplicationPool2")
					create_file("#{@support_dir}/agents/PassengerWatchdog")
					create_file("#{@support_dir}/common/libboost_oxt.a")
					create_file("#{@support_dir}/common/libpassenger_common/ApplicationPool2/Implementation.o")
				end

				it "returns that directory" do
					@locator.find_support_dir.should == @support_dir
				end
			end

			context "if there is no support directory in the home directory" do
				it "returns nil" do
					@locator.find_support_dir.should be_nil
				end
			end
		end

		context "if a custom Nginx binary is specified in the Standalone config" do
			before :each do
				create_file("#{@temp_dir}/config.json",
					JSON.dump("nginx_binary" => "/somewhere/nginx"))
			end

			it "returns that binary" do
				@locator.find_nginx_binary.should == "/somewhere/nginx"
			end
		end

		context "if no custom Nginx binary is specified in the Standalone config" do
			context "if there is an Nginx binary with the requested version in the home directory" do
				before :each do
					create_nginx(PhusionPassenger::PREFERRED_NGINX_VERSION)
				end

				it "returns that binary" do
					@locator.find_nginx_binary.should == @nginx_filename
				end
			end

			it "returns nil if there is no Nginx binary with the requested version in the home directory" do
				@locator.find_nginx_binary.should be_nil
			end

			it "returns nil if there is only an Nginx binary with a different version in the home directory" do
				create_nginx("0.0.1")
				@locator.find_nginx_binary.should be_nil
			end
		end

		describe "#support_dir_install_destination" do
			it "returns a directory under the home dir" do
				@locator.support_dir_install_destination.start_with?("#{@temp_dir}/").should be_true
			end
		end

		describe "#nginx_binary_install_destionation" do
			it "returns a directory under the home dir" do
				@locator.nginx_binary_install_destination.start_with?("#{@temp_dir}/").should be_true
			end
		end
	end

	context "when natively packaged" do
		before :each do
			PhusionPassenger.stub(:source_root).and_return("/locations.ini")
			PhusionPassenger.stub(:originally_packaged?).and_return(false)
			PhusionPassenger.stub(:natively_packaged?).and_return(true)
		end

		shared_examples_for "when no PASSENGER_DEBUG is set" do
			it "returns the packaged lib dir" do
				@locator.find_support_dir.should == PhusionPassenger.lib_dir
			end
		end

		context "if PASSENGER_DEBUG is set" do
			before :each do
				ENV['PASSENGER_DEBUG'] = '1'
			end

			after :each do
				ENV.delete('PASSENGER_DEBUG')
			end

			it_behaves_like "when no PASSENGER_DEBUG is set"
		end

		context "if no PASSENGER_DEBUG is set" do
			it_behaves_like "when no PASSENGER_DEBUG is set"
		end

		context "if a custom Nginx binary is specified in the Standalone config" do
			before :each do
				create_file("#{@temp_dir}/config.json",
					JSON.dump("nginx_binary" => "/somewhere/nginx"))
			end

			it "returns that binary" do
				@locator.find_nginx_binary.should == "/somewhere/nginx"
			end
		end

		context "if no custom Nginx binary is specified in the Standalone config" do
			context "if the default Nginx version is requested" do
				it "returns the location of the packaged Nginx binary" do
					@locator.find_nginx_binary.should == "#{PhusionPassenger.lib_dir}/PassengerWebHelper"
				end
			end

			context "if a non-default Nginx version is requested" do
				before :each do
					@locator = RuntimeLocator.new(@temp_dir, "0.0.1")
				end

				context "if there is an Nginx binary with the requested version in the home directory" do
					before :each do
						create_nginx("0.0.1")
					end

					it "returns that binary" do
						@locator.find_nginx_binary.should == @nginx_filename
					end
				end

				it "returns nil if there is no Nginx binary with the requested version in the home directory" do
					@locator.find_nginx_binary.should be_nil
				end

				it "returns nil if there is only an Nginx binary with a different version in the home directory" do
					create_nginx("0.0.2")
					@locator.find_nginx_binary.should be_nil
				end
			end
		end

		describe "#support_dir_install_destination" do
			it "returns nil" do
				@locator.support_dir_install_destination.should be_nil
			end
		end

		describe "#nginx_binary_install_destionation" do
			it "returns a directory under the home dir" do
				@locator.nginx_binary_install_destination.start_with?("#{@temp_dir}/").should be_true
			end
		end
	end
end

end # module Standalone
end # module PhusionPassenger

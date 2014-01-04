require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'tmpdir'
require 'fileutils'
require 'stringio'
require 'etc'
PhusionPassenger.require_passenger_lib 'message_channel'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'loader_shared_helpers'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'utils/native_support_utils'

module PhusionPassenger

describe Utils do
	include Utils
	include Utils::NativeSupportUtils
	
	specify "#to_boolean works" do
		LoaderSharedHelpers.to_boolean(nil).should be_false
		LoaderSharedHelpers.to_boolean(false).should be_false
		LoaderSharedHelpers.to_boolean(true).should be_true
		LoaderSharedHelpers.to_boolean(1).should be_true
		LoaderSharedHelpers.to_boolean(0).should be_true
		LoaderSharedHelpers.to_boolean("").should be_true
		LoaderSharedHelpers.to_boolean("true").should be_true
		LoaderSharedHelpers.to_boolean("false").should be_false
		LoaderSharedHelpers.to_boolean("bla bla").should be_true
	end
	
	specify "#split_by_null_into_hash works" do
		split_by_null_into_hash("").should == {}
		split_by_null_into_hash("foo\0bar\0").should == { "foo" => "bar" }
		split_by_null_into_hash("foo\0\0bar\0baz\0").should == { "foo" => "", "bar" => "baz" }
		split_by_null_into_hash("foo\0bar\0baz\0\0").should == { "foo" => "bar", "baz" => "" }
		split_by_null_into_hash("\0\0").should == { "" => "" }
	end
	
	describe "#passenger_tmpdir" do
		before :each do
			@old_passenger_tmpdir = Utils.passenger_tmpdir
			Utils.passenger_tmpdir = nil
		end
		
		after :each do
			Utils.passenger_tmpdir = @old_passenger_tmpdir
		end
		
		it "returns a directory under /tmp if Utils.passenger_tmpdir is nil" do
			File.dirname(passenger_tmpdir(false)).should == "/tmp"
		end
		
		it "returns a directory under /tmp if Utils.passenger_tmpdir is an empty string" do
			Utils.passenger_tmpdir = ''
			File.dirname(passenger_tmpdir(false)).should == "/tmp"
		end
		
		it "returns Utils.passenger_tmpdir if it's set" do
			Utils.passenger_tmpdir = '/foo'
			passenger_tmpdir(false).should == '/foo'
		end
		
		it "creates the directory if it doesn't exist, if the 'create' argument is true" do
			Utils.passenger_tmpdir = 'utils_spec.tmp'
			passenger_tmpdir
			begin
				File.directory?('utils_spec.tmp').should be_true
			ensure
				FileUtils.chmod_R(0777, 'utils_spec.tmp')
				FileUtils.rm_rf('utils_spec.tmp')
			end
		end
	end
	
	######################
end

end # module PhusionPassenger

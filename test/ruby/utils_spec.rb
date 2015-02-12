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

  ######################
end

end # module PhusionPassenger

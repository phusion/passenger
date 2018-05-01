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

  specify "#split_by_null_into_hash works" do
    expect(split_by_null_into_hash("")).to eq({})
    expect(split_by_null_into_hash("foo\0bar\0")).to eq("foo" => "bar")
    expect(split_by_null_into_hash("foo\0\0bar\0baz\0")).to eq("foo" => "", "bar" => "baz")
    expect(split_by_null_into_hash("foo\0bar\0baz\0\0")).to eq("foo" => "bar", "baz" => "")
    expect(split_by_null_into_hash("\0\0")).to eq("" => "")
  end

  ######################
end

end # module PhusionPassenger

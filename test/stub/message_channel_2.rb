#!/usr/bin/env ruby
source_root = File.expand_path(File.dirname(__FILE__) + "/../..")
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
require 'phusion_passenger/message_channel'

include PhusionPassenger
reader = MessageChannel.new(STDIN)
writer = MessageChannel.new(STDOUT)
writer.write_scalar(reader.read_scalar << "!!")
writer.write_scalar(reader.read_scalar << "??")
writer.close

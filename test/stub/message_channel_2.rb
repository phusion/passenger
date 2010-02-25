#!/usr/bin/env ruby
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/../../lib")
require 'phusion_passenger/message_channel'

include PhusionPassenger
reader = MessageChannel.new(STDIN)
writer = MessageChannel.new(STDOUT)
writer.write_scalar(reader.read_scalar << "!!")
writer.write_scalar(reader.read_scalar << "??")
writer.close

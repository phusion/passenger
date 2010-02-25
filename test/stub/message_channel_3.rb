#!/usr/bin/env ruby
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/../../lib")
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/../../ext")
require 'phusion_passenger/message_channel'
require 'phusion_passenger/utils'

include PhusionPassenger
channel = MessageChannel.new(IO.new(3))
channel.write(*channel.read)
channel.write_scalar(channel.read_scalar)

io = channel.recv_io
channel.send_io(io)
io.close

channel.write(*channel.read)
channel.close

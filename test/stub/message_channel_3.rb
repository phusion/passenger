#!/usr/bin/env ruby
source_root = File.expand_path(File.dirname(__FILE__) + "/../..")
$LOAD_PATH.unshift("#{source_root}/lib")
require 'socket'
require 'phusion_passenger'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/utils'

include PhusionPassenger
channel = MessageChannel.new(UNIXSocket.for_fd(3))
channel.write(*channel.read)
channel.write_scalar(channel.read_scalar)

io = channel.recv_io
channel.send_io(io)
io.close

channel.write(*channel.read)
channel.close

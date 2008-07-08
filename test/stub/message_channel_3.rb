#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../ext"
require 'passenger/message_channel'
require 'passenger/utils'

include Passenger
channel = MessageChannel.new(IO.new(3))
channel.write(*channel.read)
channel.write_scalar(channel.read_scalar)

io = channel.recv_io
channel.send_io(io)
io.close

channel.write(*channel.read)
channel.close

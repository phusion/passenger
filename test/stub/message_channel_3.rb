#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/message_channel'

include Passenger
channel = MessageChannel.new(IO.new(3))
channel.write(*channel.read)
channel.write_scalar(channel.read_scalar)

io = channel.recv_io
channel.send_io(io)
io.close

channel.write(*channel.read)
channel.close

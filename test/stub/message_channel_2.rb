#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/message_channel'

include ModRails
reader = MessageChannel.new(STDIN)
writer = MessageChannel.new(STDOUT)
writer.write_scalar(reader.read_scalar << "!!")
writer.write_scalar(reader.read_scalar << "??")
writer.close
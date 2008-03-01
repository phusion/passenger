#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/message_channel'

include Passenger
reader = MessageChannel.new(STDIN)
writer = MessageChannel.new(STDOUT)
writer.write(*(reader.read << "!!"))
writer.write(*(reader.read << "??"))

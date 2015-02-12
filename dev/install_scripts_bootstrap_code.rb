#!/usr/bin/env ruby
# encoding: utf-8

# This script changes the bootstrap code for all Phusion Passenger commands,
# as well as the Nginx module config script, so that they work no
# matter which Ruby interpreter is currently in $PATH, and no matter how
# Phusion Passenger is packaged.
#
# The bootstrap code must not add ruby_libdir to $LOAD_PATH. The active Ruby
# can be *any* Ruby interpreter, maybe not even MRI. ruby_libdir belongs to
# a Ruby interpreter installed by the distribution, and the files in it may
# may be incompatible with the active Ruby.

type = ARGV.shift

if type == "--ruby"
  ruby_libdir = ARGV.shift
  BOOTSTRAP_CODE = %Q{
    ENV["PASSENGER_LOCATION_CONFIGURATION_FILE"] = "#{ruby_libdir}/phusion_passenger/locations.ini"
    begin
      require 'rubygems'
    rescue LoadError
    end
    require '#{ruby_libdir}/phusion_passenger'
  }
elsif type == "--nginx-module-config"
  bindir = ARGV.shift
  BOOTSTRAP_CODE = %Q{
    PASSENGER_CONFIG=#{bindir}/passenger-config
  }
else
  abort "Invalid type"
end
BOOTSTRAP_CODE.gsub!(/^  (  )?/, '').strip

ARGV.each do |filename|
  File.open(filename, "r+") do |f|
    text = f.read
    text.sub!(
      /^## Magic comment: begin bootstrap ##.*## Magic comment: end bootstrap \#\#$/m,
      BOOTSTRAP_CODE)
    f.rewind
    f.truncate(0)
    f.write(text)
  end
end

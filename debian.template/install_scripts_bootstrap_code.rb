#!/usr/bin/env ruby
# encoding: utf-8

# We change the bootstrap code for all Phusion Passenger command
# so that they work no matter which Ruby interpreter is currently
# in $PATH.
# 
# We must not add ruby_libdir to $LOAD_PATH. The active Ruby can be
# *any* Ruby interpreter, maybe not even MRI. ruby_libdir belongs to
# a Ruby interpreter installed by the distribution, and the files in
# it may be incompatible with the active Ruby.

ruby_libdir = ARGV.shift

BOOTSTRAP_CODE = %Q{
	ENV["PASSENGER_LOCATION_CONFIGURATION_FILE"] = "#{ruby_libdir}/phusion_passenger/locations.ini"
	require '#{ruby_libdir}/phusion_passenger'
}
BOOTSTRAP_CODE.gsub!(/^\t/, '')

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

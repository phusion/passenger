#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

module PhusionPassenger

module Utils
protected
	@@passenger_tmpdir = nil
	
	def passenger_tmpdir(create = true)
		PhusionPassenger::Utils.passenger_tmpdir(create)
	end
	
	# Returns the directory in which to store Phusion Passenger-specific
	# temporary files. If +create+ is true, then this method creates the
	# directory if it doesn't exist.
	def self.passenger_tmpdir(create = true)
		dir = @@passenger_tmpdir
		if dir.nil? || dir.empty?
			tmpdir = "/tmp"
			["PASSENGER_TEMP_DIR", "PASSENGER_TMPDIR"].each do |name|
				if ENV.has_key?(name) && !ENV[name].empty?
					tmpdir = ENV[name]
					break
				end
			end
			dir = "#{tmpdir}/passenger.1.0.#{Process.pid}"
			dir.gsub!(%r{//+}, '/')
			@@passenger_tmpdir = dir
		end
		if create && !File.exist?(dir)
			# This is a very minimal implementation of the subdirectory
			# creation logic in ServerInstanceDir.h. This implementation
			# is only meant to make the unit tests pass. For production
			# systems one should pre-create the temp directory with
			# ServerInstanceDir.h.
			system("mkdir", "-p", "-m", "u=rwxs,g=rwx,o=rwx", dir)
			system("mkdir", "-p", "-m", "u=rwxs,g=rwx,o=rwx", "#{dir}/generation-0")
			system("mkdir", "-p", "-m", "u=rwxs,g=rwx,o=rwx", "#{dir}/backends")
		end
		return dir
	end
	
	def self.passenger_tmpdir=(dir)
		@@passenger_tmpdir = dir
	end
end

end

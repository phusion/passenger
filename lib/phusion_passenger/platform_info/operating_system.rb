#  Phusion Passenger - http://www.modrails.com/
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

require 'rbconfig'
require 'phusion_passenger/platform_info'

module PhusionPassenger

module PlatformInfo
	# Returns the operating system's name. This name is in lowercase and contains no spaces,
	# and thus is suitable to be used in some kind of ID. E.g. "linux", "macosx".
	def self.os_name
		if Config::CONFIG['target_os'] =~ /darwin/ && (sw_vers = find_command('sw_vers'))
			return "macosx"
		else
			return RUBY_PLATFORM.sub(/.*?-/, '')
		end
	end
	memoize :os_name
	
	# The current platform's shared library extension ('so' on most Unices).
	def self.library_extension
		if RUBY_PLATFORM =~ /darwin/
			return "bundle"
		else
			return "so"
		end
	end
end

end # module PhusionPassenger

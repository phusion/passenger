#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
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
module Standalone

module Utils
private
	def require_platform_info_binary_compatibility
		if !defined?(PlatformInfo) || !PlatformInfo.respond_to?(:cxx_binary_compatibility_id)
			PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
		end
	end

	def runtime_version_string(nginx_version)
		if PhusionPassenger.originally_packaged? || nginx_version != PhusionPassenger::PREFERRED_NGINX_VERSION
			require_platform_info_binary_compatibility
			return "#{VERSION_STRING}-#{PlatformInfo.passenger_binary_compatibility_id}"
		else
			return VERSION_STRING
		end
	end

	# Dir.pwd resolves symlinks. So in turn, File.expand_path/File.absolute_path
	# do that too. We work around that by shelling out to the `pwd` tool.
	if File.respond_to?(:absolute_path)
		def absolute_path(path)
			return File.absolute_path(path, `pwd`.strip)
		end
	else
		def absolute_path(path)
			return File.expand_path(path, `pwd`.strip)
		end
	end
end

end
end

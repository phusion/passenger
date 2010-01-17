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

module PhusionPassenger
	# Returns whether this Phusion Passenger installation is packaged
	# using the OS's native package management system, i.e. as opposed
	# to being installed from source or with RubyGems.
	def self.natively_packaged?
		if !defined?(@natively_packaged)
			@natively_packaged = !File.exist?("#{LIBDIR}/../Rakefile") ||
			                     !File.exist?("#{LIBDIR}/../DEVELOPERS.TXT")
		end
		return @natively_packaged
	end
	
	LIBDIR        = File.expand_path(File.dirname(__FILE__))
	TEMPLATES_DIR = File.join(LIBDIR, "phusion_passenger", "templates")
	if natively_packaged?
		require 'rbconfig'
		
		# Top directory of the Phusion Passenger source code.
		SOURCE_ROOT        = "/usr/lib/phusion_passenger/source"
		
		# Directory containing native_support.so.
		NATIVE_SUPPORT_DIR = File.join(Config::CONFIG["archdir"], "phusion_passenger")
		
		# Documentation directory.
		DOCDIR             = "/usr/share/doc/phusion_passenger"
	else
		SOURCE_ROOT        = File.expand_path(File.join(LIBDIR, ".."))
		NATIVE_SUPPORT_DIR = File.join(SOURCE_ROOT, "ext", "phusion_passenger")
		DOCDIR             = File.join(SOURCE_ROOT, "doc")
	end
	
	if $LOAD_PATH.first != LIBDIR
		$LOAD_PATH.unshift(LIBDIR)
		$LOAD_PATH.uniq!
	end
end if !defined?(PhusionPassenger)

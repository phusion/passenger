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
	
	# Phusion Passenger version number.
	# Don't forget to edit ext/common/Constants.h too.
	VERSION_STRING = '2.9.1'
	
	# Directory containing the Phusion Passenger Ruby libraries.
	LIBDIR         = File.expand_path(File.dirname(__FILE__))
	
	# Directory containing templates.
	TEMPLATES_DIR  = File.join(LIBDIR, "phusion_passenger", "templates")
	
	# Subdirectory under $HOME to use for storing resource files.
	LOCAL_DIR      = ".passenger"
	
	# Directories in which to look for plugins.
	PLUGIN_DIRS    = ["/usr/share/phusion-passenger/plugins",
		"/usr/local/share/phusion-passenger/plugins",
		"~/#{LOCAL_DIR}/plugins"]
	
	if natively_packaged?
		SOURCE_ROOT        = "/usr/lib/phusion-passenger/source"
		NATIVE_SUPPORT_DIR = "/usr/lib/phusion-passenger/native_support/#{VERSION_STRING}"
		DOCDIR             = "/usr/share/doc/phusion-passenger"
	else
		# Top directory of the Phusion Passenger source code.
		SOURCE_ROOT        = File.expand_path(File.join(LIBDIR, ".."))
		
		# Directory containing native_support.so.
		NATIVE_SUPPORT_DIR = File.join(SOURCE_ROOT, "ext", "phusion_passenger")
		
		# Documentation directory.
		DOCDIR             = File.join(SOURCE_ROOT, "doc")
	end
	
	PREFERRED_NGINX_VERSION = '0.7.65'
	PREFERRED_PCRE_VERSION  = '8.01'
	
	LITE_INTERFACE_VERSION  = 1
	
	if $LOAD_PATH.first != LIBDIR
		$LOAD_PATH.unshift(LIBDIR)
		$LOAD_PATH.uniq!
	end
end if !defined?(PhusionPassenger::LIBDIR)

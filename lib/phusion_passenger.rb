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
	###### Version numbers ######
	
	# Phusion Passenger version number. Don't forget to edit ext/common/Constants.h too.
	VERSION_STRING = '3.0.1'
	
	PREFERRED_NGINX_VERSION = '0.8.53'
	PREFERRED_PCRE_VERSION  = '8.10'
	STANDALONE_INTERFACE_VERSION  = 1
	
	
	###### Directories ######
	# Don't forget to modify ext/common/ResourceLocator.h too.
	
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
	
	NATIVELY_PACKAGED_SOURCE_ROOT        = "/usr/share/phusion-passenger/source"
	NATIVELY_PACKAGED_DOCDIR             = "/usr/share/doc/phusion-passenger"
	NATIVELY_PACKAGED_RESOURCES_DIR      = "/usr/share/phusion-passenger"
	NATIVELY_PACKAGED_APACHE2_MODULE     = "/usr/lib/apache2/modules/mod_passenger.so"
	
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
	
	# Directory under $HOME for storing Phusion Passenger Standalone resource files.
	LOCAL_STANDALONE_RESOURCE_DIR  = File.join(LOCAL_DIR, "standalone")
	
	# System-wide directory for storing Phusion Passenger Standalone resource files.
	GLOBAL_STANDALONE_RESOURCE_DIR = "/var/lib/passenger-standalone"
	
	if !natively_packaged?
		# Top directory of the Phusion Passenger source code.
		SOURCE_ROOT        = File.expand_path(File.join(LIBDIR, ".."))
		
		# Documentation directory.
		DOCDIR             = File.join(SOURCE_ROOT, "doc")
		
		# Directory containing Phusion Passenger resource files.
		RESOURCES_DIR      = File.join(SOURCE_ROOT, "resources")
		
		# Location of the Apache 2 module.
		APACHE2_MODULE     = File.join(SOURCE_ROOT, "ext", "apache2", "mod_passenger.so")
		
		# Directory possibly containing #{archdir}/passenger_native_support.so.
		# Not available when natively packaged.
		NATIVE_SUPPORT_DIR = File.join(SOURCE_ROOT, "ext", "ruby")
	else
		SOURCE_ROOT        = NATIVELY_PACKAGED_SOURCE_ROOT
		DOCDIR             = NATIVELY_PACKAGED_DOCDIR
		RESOURCES_DIR      = NATIVELY_PACKAGED_RESOURCES_DIR
		APACHE2_MODULE     = NATIVELY_PACKAGED_APACHE2_MODULE
	end
	
	
	###### Other resource locations ######
	
	STANDALONE_BINARIES_URL_ROOT  = "http://standalone-binaries.modrails.com"
	
	
	if $LOAD_PATH.first != LIBDIR
		$LOAD_PATH.unshift(LIBDIR)
		$LOAD_PATH.uniq!
	end
end if !defined?(PhusionPassenger::LIBDIR)

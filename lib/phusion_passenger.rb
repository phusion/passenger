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
	FILE_LOCATION = File.expand_path(__FILE__)
	
	
	###### Version numbers ######
	
	# Phusion Passenger version number. Don't forget to edit ext/common/Constants.h too.
	VERSION_STRING = '3.0.9'
	
	PREFERRED_NGINX_VERSION = '1.0.6'
	PREFERRED_PCRE_VERSION  = '8.12'
	STANDALONE_INTERFACE_VERSION  = 1
	
	
	###### Directories ######
	
	NAMESPACE_DIRNAME            = "phusion-passenger"
	STANDALONE_NAMESPACE_DIRNAME = "passenger-standalone"
	
	# Subdirectory under $HOME to use for storing resource files.
	LOCAL_DIR = ".passenger"
	
	# Directories in which to look for plugins.
	PLUGIN_DIRS = [
		"/usr/share/#{NAMESPACE_DIRNAME}/plugins",
		"/usr/local/share/#{NAMESPACE_DIRNAME}/plugins",
		"~/#{LOCAL_DIR}/plugins"
	]
	
	# Directory under $HOME for storing Phusion Passenger Standalone runtime files.
	LOCAL_STANDALONE_RESOURCE_DIR  = File.join(LOCAL_DIR, "standalone")
	
	# System-wide directory for storing Phusion Passenger Standalone runtime files.
	GLOBAL_STANDALONE_RESOURCE_DIR = "/var/lib/#{STANDALONE_NAMESPACE_DIRNAME}"
	
	NATIVELY_PACKAGED_BIN_DIR                = "/usr/bin".freeze
	NATIVELY_PACKAGED_AGENTS_DIR             = "/usr/lib/#{NAMESPACE_DIRNAME}/agents".freeze
	NATIVELY_PACKAGED_HELPER_SCRIPTS_DIR     = "/usr/share/#{NAMESPACE_DIRNAME}/helper-scripts".freeze
	NATIVELY_PACKAGED_RESOURCES_DIR          = "/usr/share/#{NAMESPACE_DIRNAME}".freeze
	NATIVELY_PACKAGED_DOC_DIR                = "/usr/share/doc/#{NAMESPACE_DIRNAME}".freeze
	NATIVELY_PACKAGED_COMPILABLE_SOURCE_DIR  = "/usr/share/#{NAMESPACE_DIRNAME}/compilable-source".freeze
	NATIVELY_PACKAGED_RUNTIME_LIBDIR         = "/usr/lib/#{NAMESPACE_DIRNAME}"
	NATIVELY_PACKAGED_HEADER_DIR             = "/usr/include/#{NAMESPACE_DIRNAME}"
	NATIVELY_PACKAGED_APACHE2_MODULE         = "/usr/lib/apache2/modules/mod_passenger.so".freeze
	
	# Follows the logic of ext/common/ResourceLocator.h, so don't forget to modify that too.
	def self.locate_directories(root_or_file = nil)
		root_or_file ||= find_root_or_locations_file
		@root = root_or_file
		
		if File.file?(root_or_file)
			filename = root_or_file
			options  = {}
			in_locations_section = false
			File.open(filename, 'r') do |f|
				while !f.eof?
					line = f.readline
					line.strip!
					next if line.empty?
					if line =~ /\A\[(.+)\]\Z/
						in_locations_section = $1 == 'locations'
					elsif in_locations_section && line =~ /=/
						key, value = line.split(/ *= */, 2)
						options[key.freeze] = value.freeze
					end
				end
			end
			
			@originally_packaged   = false
			@bindir                = get_option(filename, options, 'bin')
			@agents_dir            = get_option(filename, options, 'agents')
			@helper_scripts_dir    = get_option(filename, options, 'helper_scripts')
			@resources_dir         = get_option(filename, options, 'resources')
			@doc_dir               = get_option(filename, options, 'doc')
			@compilable_source_dir = get_option(filename, options, 'compilable_source')
			@runtime_libdir        = get_option(filename, options, 'runtimelib')
			@header_dir            = get_option(filename, options, 'headers')
			@apache2_module_path   = get_option(filename, options, 'apache2_module')
		else
			root = root_or_file
			@originally_packaged = File.exist?("#{root}/Rakefile") &&
			                       File.exist?("#{root}/DEVELOPERS.TXT")
			if @originally_packaged
				@bin_dir               = "#{root}/bin".freeze
				@agents_dir            = "#{root}/agents".freeze
				@helper_scripts_dir    = "#{root}/helper-scripts".freeze
				@resources_dir         = "#{root}/resources".freeze
				@doc_dir               = "#{root}/doc".freeze
				@compilable_source_dir = root.dup.freeze
				@runtime_libdir        = "#{root}/ext/common"
				@header_dir            = "#{root}/ext/common"
				@apache2_module        = "#{root}/ext/apache2/mod_passenger.so".freeze
			else
				@bin_dir               = NATIVELY_PACKAGED_BIN_DIR
				@agents_dir            = NATIVELY_PACKAGED_AGENTS_DIR
				@helper_scripts_dir    = NATIVELY_PACKAGED_HELPER_SCRIPTS_DIR
				@resources_dir         = NATIVELY_PACKAGED_RESOURCES_DIR
				@doc_dir               = NATIVELY_PACKAGED_DOC_DIR
				@compilable_source_dir = NATIVELY_PACKAGED_COMPILABLE_SOURCE_DIR
				@runtime_libdir        = NATIVELY_PACKAGED_RUNTIME_LIBDIR
				@header_dir            = NATIVELY_PACKAGED_HEADER_DIR
				@apache2_module        = NATIVELY_PACKAGED_APACHE2_MODULE
			end
		end
	end
	
	def self.root
		return @root
	end
	
	# Returns whether this Phusion Passenger installation's files are all
	# located within the same directory, in the same manner as the source
	# tarball or the gem. If Phusion Passenger is installed with the OS's
	# native package manager (e.g. RPM/DEB) then the result is false.
	def self.originally_packaged?
		return @originally_packaged
	end
	
	def self.bin_dir
		return @bin_dir
	end
	
	def self.agents_dir
		return @agents_dir
	end
	
	def self.helper_scripts_dir
		return @helper_scripts_dir
	end
	
	def self.resources_dir
		return @resources_dir
	end
	
	def self.doc_dir
		return @doc_dir
	end
	
	def self.ruby_libdir
		@libdir ||= File.dirname(FILE_LOCATION)
	end
	
	def self.compilable_source_dir
		return @compilable_source_dir
	end
	
	def self.runtime_libdir
		return @runtime_libdir
	end
	
	def self.header_dir
		return @header_dir
	end
	
	def self.apache2_module_path
		return @apache2_module_path
	end
	
	
	def self.nginx_module_source_dir
		return "#{compilable_source_dir}/ext/nginx"
	end
	
	def self.templates_dir
		return "#{resources_dir}/templates"
	end
	
	def self.runtime_libraries_compiled?
		return File.exist?("#{runtime_libdir}/libpassenger_common.a") &&
			File.exist?("#{runtime_libdir}/libboost_oxt.a")
	end
	
	
	###### Other resource locations ######
	
	STANDALONE_BINARIES_URL_ROOT  = "http://standalone-binaries.modrails.com"
	
	
	if $LOAD_PATH.first != ruby_libdir
		$LOAD_PATH.unshift(ruby_libdir)
		$LOAD_PATH.uniq!
	end


private
	def self.find_root_or_locations_file
		filename = ENV['PASSENGER_ROOT']
		return filename if filename
		
		filename = "#{ruby_libdir}/locations.ini"
		return filename if File.exist?(filename)
		
		require 'etc' if !defined?(Etc)
		begin
			user = Etc.getpwuid(Process.uid)
		rescue ArgumentError
			# Unknown user.
			return File.dirname(libdir)
		end
		
		home = user.dir
		
		filename = "#{home}/#{LOCAL_DIR}/locations.ini"
		return filename if File.exist?(filename)
		
		filename = "/etc/#{NAMESPACE_DIRNAME}/locations.ini"
		return filename if File.exist?(filename)
		
		return File.dirname(ruby_libdir)
	end
	
	def self.get_option(filename, options, key)
		value = options[key]
		if value
			return value
		else
			raise "Option '#{key}' missing in file #{filename}"
		end
	end
	
	def self.get_bool_option(filename, options, key)
		value = get_option(filename, options, key)
		return value == 'yes' || value == 'true' || value == 'on' || value == '1'
	end
end if !defined?(PhusionPassenger::LIBDIR)

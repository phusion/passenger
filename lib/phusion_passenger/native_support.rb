#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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

class NativeSupportLoader
	def self.supported?
		return !defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby" || RUBY_ENGINE == "rbx"
	end

	def try_load
		if defined?(NativeSupport)
			return true
		else
			require 'phusion_passenger'
			load_from_native_support_output_dir ||
			load_from_buildout_dir ||
			load_from_load_path ||
			load_from_home_dir
		end
	end
	
	def start
		try_load || download_binary_and_load || compile_and_load
	end

private
	def archdir
		@archdir ||= begin
			PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
			PlatformInfo.ruby_extension_binary_compatibility_id
		end
	end
	
	def libext
		@libext ||= begin
			PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
			PlatformInfo.library_extension
		end
	end
	
	def home
		@home ||= begin
			require 'etc' if !defined?(Etc)
			home = Etc.getpwuid(Process.uid).dir
		end
	end
	
	def library_name
		return "passenger_native_support.#{libext}"
	end
	
	def extconf_rb
		File.join(PhusionPassenger.ruby_extension_source_dir, "extconf.rb")
	end
	
	def load_from_native_support_output_dir
		# Quick workaround for people suffering from
		# https://code.google.com/p/phusion-passenger/issues/detail?id=865
		output_dir = ENV['PASSENGER_NATIVE_SUPPORT_OUTPUT_DIR']
		if output_dir && !output_dir.empty?
			begin
				require "#{output_dir}/#{VERSION_STRING}/#{archdir}/#{library_name}"
				return true
			rescue LoadError
				return false
			end
		else
			return false
		end
	end
	
	def load_from_buildout_dir
		if PhusionPassenger.buildout_dir
			begin
				require "#{PhusionPassenger.buildout_dir}/ruby/#{archdir}/#{library_name}"
				return true
			rescue LoadError
				return false
			end
		else
			return false
		end
	end
	
	def load_from_load_path
		require 'passenger_native_support'
		return true
	rescue LoadError
		return false
	end
	
	def load_from_home_dir
		begin
			require "#{home}/#{USER_NAMESPACE_DIRNAME}/native_support/#{VERSION_STRING}/#{archdir}/#{library_name}"
			return true
		rescue LoadError
			return false
		end
	end

	def download_binary_and_load
		if !PhusionPassenger.installed_from_release_package?
			return
		end
		if ENV['PASSENGER_DOWNLOAD_NATIVE_SUPPORT_BINARY'] == '0'
			STDERR.puts "*** Phusion Passenger: PASSENGER_DOWNLOAD_NATIVE_SUPPORT_BINARY set, " +
				"not downloading precompiled binary"
			return
		end
		STDERR.puts "*** Phusion Passenger: no #{library_name} found for " +
			"the current Ruby interpreter. Downloading precompiled binary from the Phusion server " +
			"(set PASSENGER_DOWNLOAD_NATIVE_SUPPORT_BINARY=0 to disable)..."
		
		PhusionPassenger.require_passenger_lib 'platform_info/ruby'
		PhusionPassenger.require_passenger_lib 'utils/tmpio'
		PhusionPassenger::Utils.mktmpdir("passenger-native-support-") do |dir|
			Dir.chdir(dir) do
				basename = "rubyext-#{archdir}.tar.gz"
				if !download(basename, dir)
					return false
				end

				sh "tar", "xzf", basename
				sh "rm", "-f", basename
				STDERR.puts "Checking whether downloaded binary is usable..."

				File.open("test.rb", "w") do |f|
					f.puts(%Q{
						require File.expand_path('passenger_native_support')
						f = File.open("test.txt", "w")
						PhusionPassenger::NativeSupport.writev(f.fileno, ["hello", "\n"])
					})
				end

				if sh_nonfatal("#{PlatformInfo.ruby_command} -I. test.rb") &&
				   File.exist?("test.txt") &&
				   File.read("test.txt") == "hello\n"
					STDERR.puts "Binary is usable."
					File.unlink("test.rb")
					File.unlink("test.txt")
					result = try_directories(installation_target_dirs) do |target_dir|
						files = Dir["#{dir}/*"]
						STDERR.puts "# Installing " + files.map{ |n| File.basename(n) }.join(' ')
						FileUtils.cp(files, target_dir)
						require "#{target_dir}/#{library_name}"
						[true, false]
					end
					return result
				else
					STDERR.puts "Binary is not usable."
					return false
				end
			end
		end
	end

	def compile_and_load
		if ENV['PASSENGER_COMPILE_NATIVE_SUPPORT_BINARY'] == '0'
			STDERR.puts "*** Phusion Passenger: PASSENGER_COMPILE_NATIVE_SUPPORT_BINARY set, " +
				"not compiling native_support"
			return false
		end

		STDERR.puts "*** Phusion Passenger: no #{library_name} found for " +
			"the current Ruby interpreter. Compiling one (set " +
			"PASSENGER_COMPILE_NATIVE_SUPPORT_BINARY=0 to disable)..."

		require 'fileutils'
		PhusionPassenger.require_passenger_lib 'platform_info/ruby'
		
		target_dir = compile(installation_target_dirs)
		if target_dir
			require "#{target_dir}/#{library_name}"
		else
			STDERR.puts "Ruby native_support extension not loaded. Continuing without native_support."
			return false
		end
	end

	def installation_target_dirs
		target_dirs = []
		if (output_dir = ENV['PASSENGER_NATIVE_SUPPORT_OUTPUT_DIR']) && !output_dir.empty?
			target_dirs << "#{output_dir}/#{VERSION_STRING}/#{archdir}"
		end
		if PhusionPassenger.buildout_dir
			target_dirs << "#{PhusionPassenger.buildout_dir}/ruby/#{archdir}"
		end
		target_dirs << "#{home}/#{USER_NAMESPACE_DIRNAME}/native_support/#{VERSION_STRING}/#{archdir}"
		return target_dirs
	end

	def download(name, output_dir)
		url = "#{PhusionPassenger::BINARIES_URL_ROOT}/#{PhusionPassenger::VERSION_STRING}/#{name}"
		filename = "#{output_dir}/#{name}"
		
		cache_dir = PhusionPassenger.download_cache_dir
		if File.exist?("#{cache_dir}/#{name}")
			FileUtils.cp("#{cache_dir}/#{name}", filename, :verbose => true)
			return true
		end

		STDERR.puts "Attempting to download #{url} into #{output_dir}"
		cert = PhusionPassenger.binaries_ca_cert_path
		File.unlink("#{filename}.tmp") rescue nil
		if PhusionPassenger::PlatformInfo.find_command("wget")
			result = system("wget", "--tries=3", "-O", "#{filename}.tmp", "--ca-certificate=#{cert}", url)
		else
			result = system("curl", url, "-f", "-L", "-o", "#{filename}.tmp", "--cacert", cert)
		end
		if result
			File.rename("#{filename}.tmp", filename)
		else
			File.unlink("#{filename}.tmp") rescue nil
		end
		return result
	end
	
	def mkdir(dir)
		begin
			STDERR.puts "# mkdir -p #{dir}"
			FileUtils.mkdir_p(dir)
		rescue Errno::EEXIST
		end
	end
	
	def sh(*args)
		if !sh_nonfatal(*args)
			command_string = args.join(' ')
			raise "Could not compile #{library_name} (\"#{command_string}\" failed)"
		end
	end

	def sh_nonfatal(*args)
		command_string = args.join(' ')
		STDERR.puts "# #{command_string}"
		return system(*args)
	end
	
	def compile(target_dirs)
		try_directories(target_dirs) do |target_dir|
			result =
				sh_nonfatal("#{PlatformInfo.ruby_command} '#{extconf_rb}'") &&
				sh_nonfatal("make")
			if result
				STDERR.puts "Compilation succesful."
				STDERR.puts "-------------------------------"
				[target_dir, false]
			else
				STDERR.puts "Compilation failed."
				STDERR.puts "-------------------------------"
				[nil, false]
			end
		end
	end

	def try_directories(dirs)
		result = nil
		dirs.each_with_index do |dir, i|
			begin
				mkdir(dir)
				File.open("#{dir}/.permission_test", "w").close
				File.unlink("#{dir}/.permission_test")
				STDERR.puts "# cd #{dir}"
				Dir.chdir(dir) do
					result, should_retry = yield(dir)
					return result if !should_retry
				end
			rescue Errno::EACCES
				# If we encountered a permission error, then try
				# the next target directory. If we get a permission
				# error on the last one too then propagate the
				# exception.
				if i == dirs.size - 1
					STDERR.puts "Encountered permission error, " +
						"but no more directories to try. Giving up."
					STDERR.puts "-------------------------------"
					return nil
				else
					STDERR.puts "Encountered permission error, " +
						"trying a different directory..."
					STDERR.puts "-------------------------------"
				end
			rescue Errno::ENOTDIR
				# This can occur when locations.ini set buildout_dir
				# to an invalid path. Just ignore this error.
				if i == dirs.size - 1
					STDERR.puts "Not a valid directory, " +
						"but no more directories to try. Giving up."
					STDERR.puts "-------------------------------"
					return nil
				else
					STDERR.puts "Not a valid directory. Trying a different one..."
					STDERR.puts "-------------------------------"
				end
			end
		end
	end
end

end # module PhusionPassenger

if PhusionPassenger::NativeSupportLoader.supported?
	PhusionPassenger::NativeSupportLoader.new.start
end

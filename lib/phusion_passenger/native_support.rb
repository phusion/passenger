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

class NativeSupportLoader
	def supported?
		return !defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby" || RUBY_ENGINE == "rbx"
	end
	
	def start
		require 'phusion_passenger'
		load_from_source_dir ||
		load_from_load_path ||
		load_from_home ||
		compile_and_load
	end

private
	def archdir
		@archdir ||= begin
			require 'phusion_passenger/platform_info/binary_compatibility'
			PlatformInfo.ruby_extension_binary_compatibility_ids.join("-")
		end
	end
	
	def libext
		@libext ||= begin
			require 'phusion_passenger/platform_info/operating_system'
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
		File.join(SOURCE_ROOT, "ext", "ruby", "extconf.rb")
	end
	
	def load_from_source_dir
		if defined?(NATIVE_SUPPORT_DIR)
			begin
				require "#{NATIVE_SUPPORT_DIR}/#{archdir}/#{library_name}"
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
	
	def load_from_home
		begin
			require "#{home}/#{LOCAL_DIR}/native_support/#{VERSION_STRING}/#{archdir}/#{library_name}"
			return true
		rescue LoadError
			return false
		end
	end
	
	def compile_and_load
		STDERR.puts "*** Phusion Passenger: no #{library_name} found for " +
			"the current Ruby interpreter. Compiling one..."
		
		require 'fileutils'
		require 'phusion_passenger/platform_info/ruby'
		
		target_dirs = []
		if defined?(NATIVE_SUPPORT_DIR)
			target_dirs << "#{NATIVE_SUPPORT_DIR}/#{archdir}"
		end
		target_dirs << "#{home}/#{LOCAL_DIR}/native_support/#{VERSION_STRING}/#{archdir}"
		
		target_dir = compile(target_dirs)
		require "#{target_dir}/#{library_name}"
	end
	
	def mkdir(dir)
		begin
			STDERR.puts "# mkdir -p #{dir}"
			FileUtils.mkdir_p(dir)
		rescue Errno::EEXIST
		end
	end
	
	def sh(*args)
		command_string = args.join(' ')
		STDERR.puts "# #{command_string}"
		if !system(*args)
			raise "Could not compile #{library_name} ('#{command_string}' failed)"
		end
	end
	
	def compile(target_dirs)
		result = nil
		target_dirs.each_with_index do |target_dir, i|
			begin
				mkdir(target_dir)
				File.open("#{target_dir}/.permission_test", "w").close
				File.unlink("#{target_dir}/.permission_test")
				STDERR.puts "# cd #{target_dir}"
				Dir.chdir(target_dir) do
					sh("#{PlatformInfo.ruby_command} '#{extconf_rb}'")
					sh("make")
				end
				result = target_dir
				break
			rescue Errno::EACCES
				# If we encountered a permission error, then try
				# the next target directory. If we get a permission
				# error on the last one too then propagate the
				# exception.
				if i == target_dirs.size - 1
					raise
				else
					STDERR.puts "Encountered permission error, " +
						"trying a different directory..."
					STDERR.puts "-------------------------------"
				end
			end
		end
		return result
	end
end

end

loader = PhusionPassenger::NativeSupportLoader.new
loader.start if loader.supported?

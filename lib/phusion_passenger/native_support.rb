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
	require 'phusion_passenger'
	require 'phusion_passenger/platform_info/operating_system'
	require 'phusion_passenger/platform_info/binary_compatibility'
	
	libext  = PlatformInfo.library_extension
	archdir = PlatformInfo.ruby_extension_binary_compatibility_ids.join("-")
	loaded  = false
	begin
		require "#{NATIVE_SUPPORT_DIR}/#{archdir}/native_support.#{libext}"
		loaded = true
	rescue LoadError
		require 'etc'
		
		home = Etc.getpwuid(Process.uid).dir
		begin
			require "#{home}/#{LOCAL_DIR}/native_support/#{VERSION_STRING}/#{archdir}/native_support.#{libext}"
			loaded = true
		rescue LoadError
		end
	end
	
	if !loaded
		STDERR.puts "*** Phusion Passenger: no native_support.#{libext} found for " +
			"the current Ruby interpreter. Compiling one..."
		
		require 'fileutils'
		require 'phusion_passenger/platform_info/ruby'
		
		mkdir = proc do |dir|
			begin
				STDERR.puts "# mkdir -p #{dir}"
				FileUtils.mkdir_p(dir)
			rescue Errno::EEXIST
			end
		end
		
		sh = proc do |*args|
			command_string = args.join(' ')
			STDERR.puts "# #{command_string}"
			if !system(*args)
				raise "Could not compile native_support.#{libext} ('#{command_string}' failed)"
			end
		end
		
		compile = proc do |target_dirs|
			result = nil
			target_dirs.each_with_index do |target_dir, i|
				begin
					mkdir.call(target_dir)
					File.open("#{target_dir}/.permission_test", "w").close
					File.unlink("#{target_dir}/.permission_test")
					STDERR.puts "# cd #{target_dir}"
					Dir.chdir(target_dir) do
						extconf_rb = File.join(SOURCE_ROOT, "ext",
							"phusion_passenger", "extconf.rb")
						sh.call(PlatformInfo::RUBY, extconf_rb)
						sh.call("make")
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
			result
		end
		
		target_dir = compile.call([
			"#{NATIVE_SUPPORT_DIR}/#{archdir}",
			"#{home}/#{LOCAL_DIR}/native_support/#{VERSION_STRING}/#{archdir}"
		])
		require "#{target_dir}/native_support.#{libext}"
	end
end if !defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby" || RUBY_ENGINE == "rbx"

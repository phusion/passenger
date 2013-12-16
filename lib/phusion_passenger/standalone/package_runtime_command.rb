# encoding: utf-8
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
module Standalone

class PackageRuntimeCommand < Command
	def self.description
		return "Package the Phusion Passenger Standalone runtime."
	end

	def self.require_libs
		PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
		PhusionPassenger.require_passenger_lib 'standalone/runtime_installer'
	end

	def run
		destdir = File.expand_path("passenger-standalone")
		description =
			"Package the Phusion Passenger Standalone runtime into the specified directory.\n" <<
			"If DIRECTORY is not given then #{destdir} will be used."
		parse_options!("package [directory]", description) do |opts|
			opts.on("--nginx-version VERSION", String,
				wrap_desc("Nginx version to use as core (default: #{@options[:nginx_version]})")) do |value|
				@options[:nginx_version] = value
			end
			opts.on("--nginx-tarball FILENAME", String,
				wrap_desc("Use the given tarball instead of downloading from the Internet. " +
					"This tarball *must* match the version specified by --nginx-version!")) do |value|
				@options[:nginx_tarball] = value
			end
		end
		
		destdir     = File.expand_path(@args[0]) if @args[0]
		runtime_dir = "#{destdir}/#{PhusionPassenger::VERSION_STRING}"
		support_dir = "#{runtime_dir}/support-#{PlatformInfo.cxx_binary_compatibility_id}"
		ruby_dir    = "#{runtime_dir}/rubyext-#{PlatformInfo.ruby_extension_binary_compatibility_id}"
		nginx_dir   = "#{runtime_dir}/nginx-#{@options[:nginx_version]}-#{PlatformInfo.cxx_binary_compatibility_id}"
		
		sh "rm", "-rf", support_dir
		sh "rm", "-rf", nginx_dir
		
		installer = RuntimeInstaller.new(
			:targets     => [:nginx, :ruby, :support_binaries],
			:support_dir => support_dir,
			:ruby_dir    => ruby_dir,
			:nginx_dir   => nginx_dir,
			:nginx_version     => @options[:nginx_version],
			:nginx_tarball     => @options[:nginx_tarball],
			:download_binaries => false)
		installer.run
		
		Dir.chdir(support_dir) do
			support_dir_name = File.basename(support_dir)
			puts "cd #{support_dir}"
			sh "tar -c . | gzip --best > ../#{support_dir_name}.tar.gz"
		end
		Dir.chdir(ruby_dir) do
			ruby_dir_name = File.basename(ruby_dir)
			puts "cd #{ruby_dir}"
			sh "tar -c . | gzip --best > ../#{ruby_dir_name}.tar.gz"
		end
		Dir.chdir(nginx_dir) do
			nginx_dir_name   = File.basename(nginx_dir)
			puts "cd #{nginx_dir}"
			sh "tar -c . | gzip --best > ../#{nginx_dir_name}.tar.gz"
		end
		puts "cd #{runtime_dir}"
		sh "rm", "-rf", support_dir, ruby_dir, nginx_dir
	end

private
	def sh(*args)
		puts args.join(' ')
		if !system(*args)
			STDERR.puts "*** Cannot run command: #{args.join(' ')}"
			exit 1
		end
	end
end

end
end

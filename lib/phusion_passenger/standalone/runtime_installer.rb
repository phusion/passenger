#  encoding: utf-8
#
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
require 'fileutils'
require 'phusion_passenger'
require 'phusion_passenger/abstract_installer'
require 'phusion_passenger/packaging'
require 'phusion_passenger/common_library'
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/platform_info/binary_compatibility'
require 'phusion_passenger/standalone/utils'
require 'phusion_passenger/utils/tmpio'

module PhusionPassenger
module Standalone

# Installs the Phusion Passenger Standalone runtime by downloading or compiling
# the Phusion Passenger support binaries and Nginx, and then storing them
# in the designated directories. This installer is entirely non-interactive.
#
# The following option must be given:
# - targets: An array containing at least one of:
#   * :support_binaries - to indicate that you want to install the
#                         Phusion Passenger support binary files.
#   * :nginx - to indicate that you want to install Nginx.
#
# If `targets` contains `:support_binaries`, then you must also specify this
# options:
# - support_dir: The support binary files will be installed here.
#
# If `targets` contains `:nginx`, then you must also specify these options:
# - nginx_dir: Nginx will be installed into this directory.
# - lib_dir: Path to the Phusion Passenger libraries, which Nginx will link to.
#            This may be the same path as `support_dir`; Nginx will be compiled
#            after the support binary files are installed.
# - nginx_version (optional): The Nginx version to download. If not given then a
#   hardcoded version number will be used.
# - nginx_tarball (optional): The location to the Nginx tarball. This tarball *must*
#   contain the Nginx version as specified by +version+. If +tarball+ is given
#   then Nginx will not be downloaded; it will be extracted from this tarball
#   instead.
#
# Other optional options:
# - download_binaries: If true then RuntimeInstaller will attempt to download
#   a precompiled Nginx binary and precompiled Phusion Passenger support binaries
#   from the network, if they exist for the current platform. The default is
#   true. Note that binary downloading only happens when Phusion Passenger is
#   installed from an official release package.
# - binaries_url_root: The URL on which to look for the aforementioned binaries.
#   The default points to the Phusion website.
class RuntimeInstaller < AbstractInstaller
	include Utils

	def initialize(*args)
		super(*args)
		raise ArgumentError, "At least one target must be given" if @targets.nil? || @targets.empty?
		if @targets.include?(:support_binaries)
			if PhusionPassenger.natively_packaged?
				raise ArgumentError, "You cannot specify :support_binaries as a " +
					"target when natively packaged"
			end
			raise ArgumentError, ":support_dir must be given" if !@support_dir
		end
		if @targets.include?(:nginx)
			raise ArgumentError, ":nginx_dir must be given" if !@nginx_dir
			raise ArgumentError, ":lib_dir must be given" if !@lib_dir
		end
	end
	
protected
	def dependencies
		specs = [
			'depcheck_specs/compiler_toolchain',
			'depcheck_specs/ruby',
			'depcheck_specs/gems',
			'depcheck_specs/libs',
			'depcheck_specs/utilities'
		]
		ids = [
			'gcc',
			'g++',
			'gmake',
			'ruby-openssl',
			'rubygems',
			'rake',
			'rack',
			'libcurl-dev',
			'openssl-dev',
			'zlib-dev',
			'pcre-dev',
			'daemon_controller >= 1.1.0'
		].compact
		return [specs, ids]
	end
	
	def users_guide
		return "#{PhusionPassenger.doc_dir}/Users guide Standalone.html"
	end

	def run_steps
		show_welcome_screen if @nginx_dir
		check_whether_os_is_broken
		check_for_download_tool
		download_or_compile_binaries
		puts
		puts "<green><b>All done!</b></green>"
		puts
	end

	def before_install
		super
		@plugin.call_hook(:runtime_installer_start, self) if @plugin
		@working_dir = PhusionPassenger::Utils.mktmpdir("passenger.", PlatformInfo.tmpexedir)
		@nginx_version ||= PREFERRED_NGINX_VERSION
		@download_binaries = true if !defined?(@download_binaries)
		@binaries_url_root ||= BINARIES_URL_ROOT
	end

	def after_install
		super
		FileUtils.remove_entry_secure(@working_dir) if @working_dir
		@plugin.call_hook(:runtime_installer_cleanup) if @plugin
	end

private
	def show_welcome_screen
		render_template 'standalone/welcome',
			:version => @nginx_version,
			:dir => @nginx_dir
		puts
	end

	def check_for_download_tool
		# TODO
	end

	def download_or_compile_binaries
		if should_install_support_binaries?
			support_binaries_path = download_support_binaries
		end
		if should_install_nginx?
			nginx_binary_path = download_nginx_binary
		end
		
		should_compile_support_binaries = should_install_support_binaries? &&
			!support_binaries_path
		should_compile_nginx = should_install_nginx? && !nginx_binary_path

		if should_compile_support_binaries || should_compile_nginx
			check_dependencies(false) || exit(1)
			puts
			if should_compile_support_binaries
				check_whether_we_can_write_to(@support_dir) || exit(1)
			end
			if should_compile_nginx
				check_whether_we_can_write_to(@nginx_dir) || exit(1)
			end
		end

		if should_compile_nginx
			nginx_source_dir = download_and_extract_nginx_sources
		end
		if should_compile_support_binaries
			compile_support_binaries
		end
		if should_compile_nginx
			compile_nginx(nginx_source_dir)
		end
	end

	# If this method returns true, then PhusionPassenger.originally_packaged? is also true.
	def should_install_support_binaries?
		return @targets.include?(:support_binaries)
	end

	def should_install_nginx?
		return @targets.include?(:nginx)
	end

	def should_download_binaries?
		return PhusionPassenger.installed_from_release_package? &&
			@download_binaries &&
			@binaries_url_root
	end

	def download_support_binaries
		return nil if !should_download_binaries?

		puts "<banner>Downloading Passenger support binaries for your platform, if available...</banner>"
		basename = "support-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
		url      = "#{@binaries_url_root}/#{PhusionPassenger::VERSION_STRING}/#{basename}"
		tarball  = "#{@working_dir}/#{basename}"
		if !download(url, tarball, :cacert => PhusionPassenger.binaries_ca_cert_path, :use_cache => true)
			puts "<b>No binaries are available for your platform. But don't worry, the " +
				"necessary binaries will be compiled from source instead.</b>"
			puts
			return nil
		end
		
		FileUtils.mkdir_p(@support_dir)
		Dir.chdir(@support_dir) do
			puts "Extracting tarball..."
			return extract_tarball(tarball)
		end
	rescue Interrupt
		exit 2
	end

	def download_nginx_binary
		return false if !should_download_binaries?

		puts "<banner>Downloading Nginx binary for your platform, if available...</banner>"
		basename = "nginx-#{@nginx_version}-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
		url      = "#{@binaries_url_root}/#{PhusionPassenger::VERSION_STRING}/#{basename}"
		tarball  = "#{@working_dir}/#{basename}"
		if !download(url, tarball, :cacert => PhusionPassenger.binaries_ca_cert_path, :use_cache => true)
			puts "<b>No binary available for your platform. But don't worry, the " +
				"necessary binary will be compiled from source instead.</b>"
			puts
			return nil
		end

		FileUtils.mkdir_p(@nginx_dir)
		Dir.chdir(@nginx_dir) do
			puts "Extracting tarball..."
			return extract_tarball(tarball)
		end
	rescue Interrupt
		exit 2
	end

	def download_and_extract_nginx_sources
		begin_progress_bar
		puts "Downloading Nginx..."
		if @nginx_tarball
			tarball  = @nginx_tarball
		else
			basename = "nginx-#{@nginx_version}.tar.gz"
			tarball  = "#{@working_dir}/#{basename}"
			if !download("http://nginx.org/download/#{basename}", tarball)
				puts
				show_possible_solutions_for_download_and_extraction_problems
				exit(1)
			end
		end
		nginx_sources_name = "nginx-#{@nginx_version}"
		
		Dir.chdir(@working_dir) do
			begin_progress_bar
			begin
				result = extract_tarball(tarball) do |progress, total|
					show_progress(progress / total * 0.1, 1.0, 1, 1, "Extracting Nginx sources...")
				end
			rescue Exception
				puts
				raise
			end
			if result
				return "#{@working_dir}/#{nginx_sources_name}"
			else
				puts
				show_possible_solutions_for_download_and_extraction_problems
				exit(1)
			end
		end
	rescue Interrupt
		exit 2
	end

	def compile_support_binaries
		begin_progress_bar
		show_progress(0, 1, 1, 1, "Preparing Phusion Passenger...")
		Dir.chdir(PhusionPassenger.source_root) do
			args = "nginx_without_native_support" +
				" CACHING=false" +
				" OUTPUT_DIR='#{@support_dir}'"
			begin
				run_rake_task!(args) do |progress, total|
					show_progress(progress, total, 1, 1, "Compiling Phusion Passenger...")
				end
			ensure
				puts
			end

			system "rm -rf '#{@support_dir}'/agents/{*.o,*.dSYM}"
			system "rm -rf '#{@support_dir}'/common/libboost_oxt"
			system "rm -rf '#{@support_dir}'/*/{*.lo,*.h,*.log,Makefile,libtool,stamp-h1,config.status,.deps}"
			system "rm -rf '#{@support_dir}'/{libeio,libev}/*.o"
			
			# Retain only the object files that are needed for linking the Phusion Passenger module into Nginx.
			nginx_libs = COMMON_LIBRARY.
				only(*NGINX_LIBS_SELECTOR).
				set_output_dir("#{@support_dir}/libpassenger_common").
				link_objects
			Dir["#{@support_dir}/libpassenger_common/**/*"].each do |filename|
				if !nginx_libs.include?(filename) && File.file?(filename)
					File.unlink(filename)
				end
			end
		end
	end

	def compile_nginx(nginx_source_dir)
		install_nginx_from_source(nginx_source_dir) do |progress, total, status_text|
			show_progress(0.1 + progress / total.to_f * 0.9, 1.0, 1, 1, status_text)
		end
	end
	
	def check_whether_we_can_write_to(dir)
		FileUtils.mkdir_p(dir)
		File.new("#{dir}/__test__.txt", "w").close
		return true
	rescue
		new_screen
		if Process.uid == 0
			render_template 'standalone/cannot_write_to_dir', :dir => dir
		else
			render_template 'standalone/run_installer_as_root', :dir => dir
		end
		return false
	ensure
		File.unlink("#{dir}/__test__.txt") rescue nil
	end
	
	def show_progress(progress, total, phase, total_phases, status_text = "")
		if !phase.is_a?(Range)
			phase = phase..phase
		end
		total_progress = (phase.first - 1).to_f / total_phases
		total_progress += (progress.to_f / total) * ((phase.last - phase.first + 1).to_f / total_phases)
		
		max_width = 79
		progress_bar_width = 45
		text = sprintf("[%-#{progress_bar_width}s] %s",
			'*' * (progress_bar_width * total_progress).to_i,
			status_text)
		text = text.ljust(max_width)
		text = text[0 .. max_width - 1]
		if @stdout.tty?
			@stdout.write("#{text}\r")
			@stdout.flush
		else
			if @last_status_text != status_text
				@last_status_text = status_text
				@stdout.write("[#{status_text.sub(/\.*$/, '')}]")
			end
			@stdout.write(".")
			@stdout.flush
		end
		@plugin.call_hook(:runtime_installer_progress, total_progress, status_text) if @plugin
	end
	
	def myself
		return `whoami`.strip
	end
	
	def begin_progress_bar
		if !@begun
			@begun = true
			puts "<banner>Installing Phusion Passenger Standalone...</banner>"
		end
	end
	
	def show_possible_solutions_for_download_and_extraction_problems
		new_screen
		render_template "standalone/possible_solutions_for_download_and_extraction_problems"
		puts
	end
	
	def extract_tarball(filename)
		File.open(filename, 'rb') do |f|
			IO.popen("tar xzf -", "wb") do |io|
				buffer = ''
				buffer = buffer.force_encoding('binary') if buffer.respond_to?(:force_encoding)
				total_size = File.size(filename)
				bytes_read = 0
				yield(bytes_read, total_size) if block_given?
				begin
					doing_our_io = true
					while !f.eof?
						f.read(1024 * 8, buffer)
						io.write(buffer)
						io.flush
						bytes_read += buffer.size
						doing_our_io = false
						yield(bytes_read, total_size) if block_given?
						doing_our_io = true
					end
				rescue Errno::EPIPE
					if doing_our_io
						return false
					else
						raise
					end
				end
			end
			if $?.exitstatus != 0
				return false
			end
		end
		return true
	end
	
	def run_command_with_throbber(command, status_text)
		backlog = ""
		IO.popen("#{command} 2>&1", "r") do |io|
			throbbers = ['-', '\\', '|', '/']
			index = 0
			while !io.eof?
				backlog << io.readline
				yield("#{status_text} #{throbbers[index]}")
				index = (index + 1) % throbbers.size
			end
		end
		if $?.exitstatus != 0
			@stderr.puts
			@stderr.puts backlog
			@stderr.puts "*** ERROR: command failed: #{command}"
			exit 1
		end
	end
	
	def copy_files(files, target)
		FileUtils.mkdir_p(target)
		files.each_with_index do |filename, i|
			next if File.directory?(filename)
			dir = "#{target}/#{File.dirname(filename)}"
			if !File.directory?(dir)
				FileUtils.mkdir_p(dir)
			end
			FileUtils.install(filename, "#{target}/#{filename}", :mode => File.stat(filename).mode)
			yield(i + 1, files.size)
		end
	end
	
	def rake
		return PlatformInfo.rake_command
	end
	
	def run_rake_task!(target)
		total_lines = `#{rake} #{target} --dry-run STDERR_TO_STDOUT=1`.split("\n").size - 1
		backlog = ""
		
		IO.popen("#{rake} #{target} --trace STDERR_TO_STDOUT=1", "r") do |io|
			progress = 1
			while !io.eof?
				line = io.readline
				if line =~ /^\*\* /
					yield(progress, total_lines)
					backlog.replace("")
					progress += 1
				else
					backlog << line
				end
			end
		end
		if $?.exitstatus != 0
			@stderr.puts
			@stderr.puts "*** ERROR: the following command failed:"
			@stderr.puts(backlog)
			exit 1
		end
	end
	
	def install_nginx_from_source(source_dir)
		require 'phusion_passenger/platform_info/compiler'
		Dir.chdir(source_dir) do
			shell = PlatformInfo.find_command('bash') || "sh"
			command = ""
			if @targets.include?(:support_binaries)
				output_dir = "#{@support_dir}/common/libpassenger_common"
				nginx_libs = COMMON_LIBRARY.only(*NGINX_LIBS_SELECTOR).
					set_output_dir(output_dir).
					link_objects_as_string
				command << "env PASSENGER_INCLUDEDIR='#{PhusionPassenger.include_dir}'" <<
					" PASSENGER_LIBS='#{nginx_libs} #{output_dir}/../libboost_oxt.a' "
			end
			# RPM thinks it's being smart by scanning binaries for
			# paths and refusing to create package if it detects any
			# hardcoded thats that point to /usr or other important
			# locations. For Phusion Passenger Standalone we do not
			# care at all what the Nginx configured prefix is because
			# we pass it its resource locations during runtime, so
			# work around the problem by configure Nginx with prefix
			# /tmp.
			command << "#{shell} ./configure --prefix=/tmp " <<
				"--with-cc-opt='-Wno-error' " <<
				"--without-http_fastcgi_module " <<
				"--without-http_scgi_module " <<
				"--without-http_uwsgi_module " <<
				"--with-http_gzip_static_module " <<
				"--with-http_stub_status_module " <<
				"'--add-module=#{PhusionPassenger.nginx_module_source_dir}'"
			run_command_with_throbber(command, "Preparing Nginx...") do |status_text|
				yield(0, 1, status_text)
			end
			
			backlog = ""
			total_lines = `#{PlatformInfo.gnu_make} --dry-run`.split("\n").size
			IO.popen("#{PlatformInfo.gnu_make} 2>&1", "r") do |io|
				progress = 1
				while !io.eof?
					line = io.readline
					backlog << line
					yield(progress, total_lines, "Compiling Nginx core...")
					progress += 1
				end
			end
			if $?.exitstatus != 0
				@stderr.puts
				@stderr.puts "*** ERROR: unable to compile Nginx."
				@stderr.puts backlog
				exit 1
			end
			
			yield(1, 1, 'Copying files...')
			if !system("cp -pR objs/nginx '#{@nginx_dir}/'")
				@stderr.puts
				@stderr.puts "*** ERROR: unable to copy Nginx binary."
				exit 1
			end
			if !strip_binary("#{@nginx_dir}/nginx")
				@stderr.puts
				@stderr.puts "*** ERROR: unable to strip debugging symbols from the Nginx binary."
				exit 1
			end
		end
	end

	def strip_binary(filename)
		return system("strip", filename)
	end
end

end # module Standalone
end # module PhusionPassenger

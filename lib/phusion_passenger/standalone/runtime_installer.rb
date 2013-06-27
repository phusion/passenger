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

# Installs the Phusion Passenger Standalone runtime by downloading and compiling
# Nginx, compiling the Phusion Passenger support binaries, and storing the
# results in the designated directories. This installer is entirely
# non-interactive.
#
# The following option must be given:
# - targets: An array containing at least one of:
#   * :nginx - to indicate that you want to compile and install Nginx.
#   * :support_binaries - to indicate that you want to compile and install the
#                         Phusion Passenger support binary files.
#   * :ruby - to indicate that you want to compile and install the Ruby
#             extension files.
#
# If 'targets' contains :nginx, then you must also specify these options:
# - nginx_dir: Nginx will be installed into this directory.
# - support_dir: Path to the Phusion Passenger support binary files.
# - nginx_version (optional): The Nginx version to download. If not given then a
#   hardcoded version number will be used.
# - nginx_tarball (optional): The location to the Nginx tarball. This tarball *must*
#   contain the Nginx version as specified by +version+. If +tarball+ is given
#   then Nginx will not be downloaded; it will be extracted from this tarball
#   instead.
#
# If targets contains ':support_binaries', then you must also specify this
# options:
# - support_dir: The support binary files will be installed here.
#
# If targets contains ':ruby', then you must also specify this option:
# - ruby_dir: The support binary files will be installed here.
#
# Other optional options:
# - download_binaries: If true then RuntimeInstaller will attempt to download
#   precompiled Nginx binaries and precompiled Phusion Passenger support binary
#   files from the network, if they exist for the current platform. The default is
#   false.
# - binaries_url_root: The URL on which to look for the aforementioned binaries.
#   The default points to the Phusion website.
#
# Please note that RuntimeInstaller will try to avoid compiling/installing things
# that don't need to be compiled/installed. This is done by checking whether some
# key files exist, and concluding that something doesn't need to be
# compiled/installed if they do. This quick check is of course not perfect; if you
# want to force a recompilation/reinstall then you should remove +nginx_dir+
# and +support_dir+ before starting this installer.
class RuntimeInstaller < AbstractInstaller
	include Utils
	
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
			'download-tool',
			PlatformInfo.passenger_needs_ruby_dev_header? ? 'ruby-dev' : nil,
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
		if @support_dir && @nginx_dir
			show_welcome_screen
		end
		check_dependencies(false) || exit(1)
		check_whether_os_is_broken
		check_whether_system_has_enough_ram
		puts
		
		phase = 1
		total_phases = 0
		
		if binary_support_files_should_be_installed?
			check_whether_we_can_write_to(@support_dir) || exit(1)
			total_phases += 4
		end
		if ruby_extension_should_be_installed?
			check_whether_we_can_write_to(@ruby_dir) || exit(1)
			total_phases += 2
		end
		if nginx_needs_to_be_installed?
			check_whether_we_can_write_to(@nginx_dir) || exit(1)
			total_phases += 4
		end
		
		if binary_support_files_should_be_installed? && should_download_binaries?
			download_and_extract_binary_support_files(@support_dir) do |progress, total|
				show_progress(progress, total, 1, 1, "Extracting Passenger binaries...")
			end
			puts
			puts
		end
		if ruby_extension_should_be_installed? && should_download_binaries?
			download_and_extract_ruby_extension(@ruby_dir) do |progress, total|
				show_progress(progress, total, 1, 1, "Extracting Ruby extension...")
			end
			puts
			puts
		end
		if nginx_needs_to_be_installed? && should_download_binaries?
			download_and_extract_nginx_binaries(@nginx_dir) do |progress, total|
				show_progress(progress, total, 1, 1, "Extracting Nginx binaries...")
			end
			puts
			puts
		end
		
		if nginx_needs_to_be_installed?
			nginx_source_dir = download_and_extract_nginx_sources do |progress, total|
				show_progress(progress, total, phase, total_phases, "Extracting...")
			end
			phase += 1
			if nginx_source_dir.nil?
				puts
				show_possible_solutions_for_download_and_extraction_problems
				exit(1)
			end
		end
		if ruby_extension_should_be_installed?
			phase += install_ruby_extension do |progress, total, subphase, status_text|
				show_progress(progress, total, phase + subphase, total_phases, status_text)
			end
		end
		if binary_support_files_should_be_installed?
			install_binary_support_files do |progress, total, subphase, status_text|
				if subphase == 0
					show_progress(progress, total, phase, total_phases, status_text)
				else
					show_progress(progress, total, phase + 1 .. phase + 3, total_phases, status_text)
				end
			end
			phase += 4
		end
		if nginx_needs_to_be_installed?
			install_nginx_from_source(nginx_source_dir) do |progress, total, status_text|
				show_progress(progress, total, phase .. phase + 2, total_phases, status_text)
			end
			phase += 3
		end
		
		puts
		puts "<green><b>All done!</b></green>"
		puts
	end
	
	def before_install
		super
		@plugin.call_hook(:runtime_installer_start, self) if @plugin
		@working_dir = PhusionPassenger::Utils.mktmpdir("passenger.", PlatformInfo.tmpexedir)
		@download_binaries = true if !defined?(@download_binaries)
		@binaries_url_root ||= STANDALONE_BINARIES_URL_ROOT
	end

	def after_install
		super
		FileUtils.remove_entry_secure(@working_dir) if @working_dir
		@plugin.call_hook(:runtime_installer_cleanup) if @plugin
	end

private
	def nginx_needs_to_be_installed?
		return @targets.include?(:nginx) &&
			!File.exist?("#{@nginx_dir}/sbin/nginx")
	end
	
	def ruby_extension_should_be_installed?
		return @targets.include?(:ruby) &&
			!File.exist?("#{@ruby_dir}/#{PlatformInfo.ruby_extension_binary_compatibility_id}")
	end
	
	def binary_support_files_should_be_installed?
		return @targets.include?(:support_binaries) && (
			!File.exist?("#{@support_dir}/buildout/agents/PassengerHelperAgent") ||
			!File.exist?("#{@support_dir}/buildout/common/libpassenger_common.a")
		)
	end
	
	def should_download_binaries?
		return @download_binaries && @binaries_url_root
	end
	
	def show_welcome_screen
		render_template 'standalone/welcome',
			:version => @nginx_version,
			:dir => @nginx_dir
		puts
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
		STDOUT.write("#{text}\r")
		STDOUT.flush
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
				yield(bytes_read, total_size)
				begin
					doing_our_io = true
					while !f.eof?
						f.read(1024 * 8, buffer)
						io.write(buffer)
						io.flush
						bytes_read += buffer.size
						doing_our_io = false
						yield(bytes_read, total_size)
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
			STDERR.puts
			STDERR.puts backlog
			STDERR.puts "*** ERROR: command failed: #{command}"
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
			STDERR.puts
			STDERR.puts "*** ERROR: the following command failed:"
			STDERR.puts(backlog)
			exit 1
		end
	end
	
	def download_and_extract_binary_support_files(target, &block)
		puts "<banner>Downloading Passenger support binaries for your platform, if available...</banner>"
		basename = "support-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
		url      = "#{@binaries_url_root}/#{PhusionPassenger::VERSION_STRING}/#{basename}"
		tarball  = "#{@working_dir}/#{basename}"
		if !download(url, tarball)
			puts "<b>Looks like it's not. But don't worry, the " +
				"necessary binaries will be compiled from source instead.</b>"
			return nil
		end
		
		FileUtils.mkdir_p(target)
		Dir.chdir(target) do
			return extract_tarball(tarball, &block)
		end
	rescue Interrupt
		exit 2
	end
	
	def download_and_extract_ruby_extension(target, &block)
		puts "<banner>Downloading Ruby extension for your Ruby and platform, if available...</banner>"
		basename = "rubyext-#{PlatformInfo.ruby_extension_binary_compatibility_id}.tar.gz"
		url      = "#{@binaries_url_root}/#{PhusionPassenger::VERSION_STRING}/#{basename}"
		tarball  = "#{@working_dir}/#{basename}"
		if !download(url, tarball)
			puts "<b>Looks like it's not. But don't worry, the " +
				"necessary binaries will be compiled from source instead.</b>"
			return nil
		end
		
		FileUtils.mkdir_p(target)
		Dir.chdir(target) do
			return extract_tarball(tarball, &block)
		end
	rescue Interrupt
		exit 2
	end
	
	def download_and_extract_nginx_binaries(target, &block)
		puts "<banner>Downloading Nginx binaries for your platform, if available...</banner>"
		basename = "nginx-#{@nginx_version}-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
		url      = "#{@binaries_url_root}/#{PhusionPassenger::VERSION_STRING}/#{basename}"
		tarball  = "#{@working_dir}/#{basename}"
		if !download(url, tarball)
			puts "<b>Looks like it's not. But don't worry, the " +
				"necessary binaries will be compiled from source instead.</b>"
			return nil
		end

		FileUtils.mkdir_p(target)
		Dir.chdir(target) do
			return extract_tarball(tarball, &block)
		end
	rescue Interrupt
		exit 2
	end
	
	def download_and_extract_nginx_sources(&block)
		if @nginx_tarball
			tarball  = @nginx_tarball
		else
			puts "<banner>Downloading Nginx...</banner>"
			basename = "nginx-#{@nginx_version}.tar.gz"
			tarball  = "#{@working_dir}/#{basename}"
			if !download("http://nginx.org/download/#{basename}", tarball)
				return nil
			end
		end
		nginx_sources_name = "nginx-#{@nginx_version}"
		
		Dir.chdir(@working_dir) do
			begin_progress_bar
			if extract_tarball(tarball, &block)
				return "#{@working_dir}/#{nginx_sources_name}"
			else
				return nil
			end
		end
	rescue Interrupt
		exit 2
	end
	
	def install_ruby_extension
		begin_progress_bar
		yield(0, 1, 0, "Preparing Ruby extension...")
		Dir.chdir(PhusionPassenger.source_root) do
			run_rake_task!("native_support CACHING=false ONLY_RUBY=yes RUBY_EXTENSION_OUTPUT_DIR='#{@ruby_dir}'") do |progress, total|
				yield(progress, total, 1, "Compiling Ruby extension...")
			end
			system "rm -rf '#{@ruby_dir}'/{*.log,*.o,Makefile}"
		end
		return 2
	end
	
	def install_binary_support_files
		begin_progress_bar
		yield(0, 1, 0, "Preparing Phusion Passenger...")
		Dir.chdir(PhusionPassenger.source_root) do
			args = "nginx_without_native_support" +
				" CACHING=false" +
				" OUTPUT_DIR='#{@support_dir}'" +
				" AGENT_OUTPUT_DIR='#{@support_dir}'" +
				" COMMON_OUTPUT_DIR='#{@support_dir}'" +
				" LIBEV_OUTPUT_DIR='#{@support_dir}/libev'" +
				" LIBEIO_OUTPUT_DIR='#{@support_dir}/libeio'"
			run_rake_task!(args) do |progress, total|
				yield(progress, total, 1, "Compiling Phusion Passenger...")
			end

			system "rm -rf '#{@support_dir}'/{*.o,*.dSYM,libboost_oxt}"
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
		return 2
	end
	
	def install_nginx_from_source(source_dir)
		require 'phusion_passenger/platform_info/compiler'
		Dir.chdir(source_dir) do
			shell = PlatformInfo.find_command('bash') || "sh"
			command = ""
			if @targets.include?(:support_binaries)
				if ENV['PASSENGER_DEBUG'] && !ENV['PASSENGER_DEBUG'].empty?
					output_dir = "#{PhusionPassenger.source_root}/buildout/common/libpassenger_common"
				else
					output_dir = "#{@support_dir}/libpassenger_common"
				end
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
				"'--add-module=#{PhusionPassenger.source_root}/ext/nginx'"
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
				STDERR.puts
				STDERR.puts "*** ERROR: unable to compile Nginx."
				STDERR.puts backlog
				exit 1
			end
			
			yield(1, 1, 'Copying files...')
			if !system("cp -pR objs/nginx '#{@nginx_dir}/'")
				STDERR.puts
				STDERR.puts "*** ERROR: unable to copy Nginx binaries."
				exit 1
			end
			if !system("strip '#{@nginx_dir}/nginx'")
				STDERR.puts
				STDERR.puts "*** ERROR: unable to strip debugging symbols from the Nginx binary."
				exit 1
			end
		end
	end
end

end # module Standalone
end # module PhusionPassenger

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
require 'fileutils'
require 'phusion_passenger'
require 'phusion_passenger/abstract_installer'
require 'phusion_passenger/packaging'
require 'phusion_passenger/dependencies'
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/standalone/utils'

module PhusionPassenger
module Standalone

# Installs the Phusion Passenger Standalone runtime by downloading and compiling
# Nginx, compiling the Phusion Passenger support binaries, and storing the
# results in the designated directories. This installer is entirely
# non-interactive.
#
# The following option must be given:
# - source_root: Path to the Phusion Passenger source root.
#
# If you want RuntimeInstaller to compile and install Nginx, then you must
# specify these options:
# - nginx_dir: Nginx will be installed into this directory.
# - support_dir: See below.
# - version (optional): The Nginx version to download. If not given then a
#   hardcoded version number will be used.
# - tarball (optional): The location to the Nginx tarball. This tarball *must*
#   contain the Nginx version as specified by +version+. If +tarball+ is given
#   then Nginx will not be downloaded; it will be extracted from this tarball
#   instead.
#
# If you want RuntimeInstaller to compile and install the Phusion Passenger
# support files, then you must specify these:
# - support_dir: The support files will be installed here. Should not equal
#   +source_root+, or funny things might happen.
#
# Other optional options:
# - download_binaries: If true then RuntimeInstaller will attempt to download
#   precompiled Nginx binaries and precompiled Phusion Passenger support files
#   from the network, if they exist for the current platform. The default is
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
		result = [
			Dependencies::GCC,
			Dependencies::GnuMake,
			Dependencies::DownloadTool,
			Dependencies::Ruby_DevHeaders,
			Dependencies::Ruby_OpenSSL,
			Dependencies::RubyGems,
			Dependencies::Rake,
			Dependencies::Rack,
			Dependencies::Curl_Dev,
			Dependencies::OpenSSL_Dev,
			Dependencies::Zlib_Dev,
			Dependencies::File_Tail,
			Dependencies::Daemon_Controller,
		]
		if Dependencies.fastthread_required?
			result << Dependencies::FastThread
		end
		if Dependencies.asciidoc_required?
			result << Dependencies::AsciiDoc
		end
		return result
	end
	
	def users_guide
		return "#{DOCDIR}/Users guide Standalone.html"
	end
	
	def install!
		if @support_dir && @nginx_dir
			show_welcome_screen
		end
		check_dependencies(false) || exit(1)
		puts
		if passenger_support_files_need_to_be_installed?
			check_whether_we_can_write_to(@support_dir) || exit(1)
		end
		if nginx_needs_to_be_installed?
			check_whether_we_can_write_to(@nginx_dir) || exit(1)
		end
		
		if passenger_support_files_need_to_be_installed? && should_download_binaries?
			download_and_extract_passenger_binaries(@support_dir) do |progress, total|
				show_progress(progress, total, 1, 1, "Extracting Passenger binaries...")
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
				show_progress(progress, total, 1, 7, "Extracting...")
			end
			if nginx_source_dir.nil?
				puts
				show_possible_solutions_for_download_and_extraction_problems
				exit(1)
			end
		end
		if passenger_support_files_need_to_be_installed?
			install_passenger_support_files do |progress, total, phase, status_text|
				if phase == 1
					show_progress(progress, total, 2, 7, status_text)
				else
					show_progress(progress, total, 3..5, 7, status_text)
				end
			end
		end
		if nginx_needs_to_be_installed?
			install_nginx_from_source(nginx_source_dir) do |progress, total, status_text|
				show_progress(progress, total, 6..7, 7, status_text)
			end
		end
		
		puts
		color_puts "<green><b>All done!</b></green>"
		puts
	end
	
	def before_install
		super
		@plugin.call_hook(:runtime_installer_start, self) if @plugin
		@working_dir = "/tmp/#{myself}-passenger-standalone-#{Process.pid}"
		FileUtils.rm_rf(@working_dir)
		FileUtils.mkdir_p(@working_dir)
		@download_binaries = true if !defined?(@download_binaries)
		@binaries_url_root ||= STANDALONE_BINARIES_URL_ROOT
	end

	def after_install
		super
		FileUtils.rm_rf(@working_dir)
		@plugin.call_hook(:runtime_installer_cleanup) if @plugin
	end

private
	def nginx_needs_to_be_installed?
		return @nginx_dir && !File.exist?("#{@nginx_dir}/sbin/nginx")
	end
	
	def passenger_support_files_need_to_be_installed?
		return @support_dir && !File.exist?("#{@support_dir}/Rakefile")
	end
	
	def should_download_binaries?
		return @download_binaries && @binaries_url_root
	end
	
	def show_welcome_screen
		render_template 'standalone/welcome',
			:version => @version,
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
			color_puts "<banner>Installing Phusion Passenger Standalone...</banner>"
		end
	end
	
	def show_possible_solutions_for_download_and_extraction_problems
		new_screen
		render_template "standalone/possible_solutions_for_download_and_extraction_problems"
		puts
	end
	
	def extract_tarball(filename)
		File.open(filename, 'rb') do |f|
			IO.popen("tar xzf -", "w") do |io|
				buffer = ''
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
			FileUtils.install(filename, "#{target}/#{filename}")
			yield(i + 1, files.size)
		end
	end
	
	def rake
		return PlatformInfo.rake_command
	end
	
	def run_rake_task!(target)
		total_lines = `#{rake} #{target} --dry-run`.split("\n").size - 1
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
	
	def download_and_extract_passenger_binaries(target, &block)
		color_puts "<banner>Downloading Passenger binaries for your platform, if available...</banner>"
		url     = "#{@binaries_url_root}/#{runtime_version_string}/support.tar.gz"
		tarball = "#{@working_dir}/support.tar.gz"
		if !download(url, tarball)
			color_puts "<b>Looks like it's not. But don't worry, the " +
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
		color_puts "<banner>Downloading Nginx binaries for your platform, if available...</banner>"
		basename = "nginx-#{@version}.tar.gz"
		url      = "#{@binaries_url_root}/#{runtime_version_string}/#{basename}"
		tarball  = "#{@working_dir}/#{basename}"
		if !download(url, tarball)
			color_puts "<b>Looks like it's not. But don't worry, the " +
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
		if @tarball
			tarball  = @tarball
		else
			color_puts "<banner>Downloading Nginx...</banner>"
			basename = "nginx-#{@version}.tar.gz"
			tarball  = "#{@working_dir}/#{basename}"
			if !download("http://sysoev.ru/nginx/#{basename}", tarball)
				return nil
			end
		end
		nginx_sources_name = "nginx-#{@version}"
		
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
	
	def install_passenger_support_files
		begin_progress_bar
		
		# Copy Phusion Passenger sources to designated directory if necessary.
		yield(0, 1, 1, "Preparing Phusion Passenger...")
		FileUtils.rm_rf(@support_dir)
		Dir.chdir(@source_root) do
			files = `#{rake} package:filelist --silent`.split("\n")
			copy_files(files, @support_dir) do |progress, total|
				yield(progress, total, 1, "Copying files...")
			end
		end
		
		# Then compile it.
		yield(0, 1, 2, "Preparing Phusion Passenger...")
		Dir.chdir(@support_dir) do
			run_rake_task!("nginx RELEASE=yes") do |progress, total|
				yield(progress, total, 2, "Compiling Phusion Passenger...")
			end
		end
	end
	
	def install_nginx_from_source(source_dir)
		require 'phusion_passenger/platform_info/compiler'
		Dir.chdir(source_dir) do
			# RPM thinks it's being smart by scanning binaries for
			# paths and refusing to create package if it detects any
			# hardcoded thats that point to /usr or other important
			# locations. For Phusion Passenger Standalone we do not
			# care at all what the Nginx configured prefix is because
			# we pass it its resource locations during runtime, so
			# work around the problem by configure Nginx with prefix
			# /tmp.
			command = "sh ./configure --prefix=/tmp " <<
				"--without-pcre " <<
				"--without-http_rewrite_module " <<
				"--without-http_fastcgi_module " <<
				"'--add-module=#{@support_dir}/ext/nginx'"
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
			if !system("mkdir -p '#{@nginx_dir}/sbin'") ||
			   !system("cp -pR objs/nginx '#{@nginx_dir}/sbin/'")
				STDERR.puts
				STDERR.puts "*** ERROR: unable to copy Nginx binaries."
				exit 1
			end
		end
	end
end

end # module Standalone
end # module PhusionPassenger

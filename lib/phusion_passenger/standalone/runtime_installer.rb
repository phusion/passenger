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
require 'logger'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'abstract_installer'
PhusionPassenger.require_passenger_lib 'packaging'
PhusionPassenger.require_passenger_lib 'common_library'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
PhusionPassenger.require_passenger_lib 'standalone/utils'
PhusionPassenger.require_passenger_lib 'utils/tmpio'

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
			'cc',
			'c++',
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
	
	def users_guide_path
		return PhusionPassenger.standalone_doc_path
	end

	def users_guide_url
		return STANDALONE_DOC_URL
	end

	def run_steps
		check_whether_os_is_broken
		check_for_download_tool
		download_or_compile_binaries
	end

	def before_install
		super
		@plugin.call_hook(:runtime_installer_start, self) if @plugin
		@working_dir = PhusionPassenger::Utils.mktmpdir("passenger.", PlatformInfo.tmpexedir)
		@nginx_version ||= PREFERRED_NGINX_VERSION
		@download_binaries = true if !defined?(@download_binaries)
		@binaries_url_root ||= PhusionPassenger.binaries_sites
	end

	def after_install
		super
		FileUtils.remove_entry_secure(@working_dir) if @working_dir
		@plugin.call_hook(:runtime_installer_cleanup) if @plugin
	end

private
	def check_for_download_tool
		PhusionPassenger.require_passenger_lib 'platform_info/depcheck'
		PlatformInfo::Depcheck.load('depcheck_specs/utilities')
		result = PlatformInfo::Depcheck.find('download-tool').check
		# Don't output anything if there is a download tool.
		# We want to be as quiet as possible.
		return if result && result[:found]

		puts "<banner>Checking for basic prerequities...</banner>"
		puts

		runner = PlatformInfo::Depcheck::ConsoleRunner.new
		runner.add('download-tool')

		result = runner.check_all
		puts
		if !result
			@download_binaries = false
			line
			puts
			render_template 'standalone/download_tool_missing',
				:runner => runner
			wait
		end
	end

	def download_or_compile_binaries
		if should_install_support_binaries?
			support_binaries_downloaded = download_support_binaries
		end
		if should_install_nginx?
			nginx_binary_downloaded = download_nginx_binary
		end
		
		should_compile_support_binaries = should_install_support_binaries? &&
			!support_binaries_downloaded
		should_compile_nginx = should_install_nginx? && !nginx_binary_downloaded

		if should_compile_support_binaries || should_compile_nginx
			if @dont_compile_runtime
				@stderr.puts "*** ERROR: Refusing to compile the Phusion Passenger Standalone runtime " +
					"because --no-compile-runtime is given."
				exit(1)
			end
			check_nginx_module_sources_available || exit(1)
			puts
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
		return false if !should_download_binaries?

		puts " --> Downloading #{PROGRAM_NAME} support binaries for your platform"
		basename = "support-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
		tarball  = "#{@working_dir}/#{basename}"
		if !download_support_file(basename, tarball)
			puts "     No binaries are available for your platform. Will compile them from source"
			return false
		end
		
		FileUtils.mkdir_p(@support_dir)
		Dir.mkdir("#{@working_dir}/support")
		Dir.chdir("#{@working_dir}/support") do
			if !extract_tarball(tarball)
				@stderr.puts " *** Error: cannot extract tarball"
				return false
			end
			return false if !check_support_binaries
		end

		if system("mv '#{@working_dir}/support'/* '#{@support_dir}'/")
			return true
		else
			@stderr.puts " *** Error: could not move extracted files to the support directory"
			return false
		end
	rescue Interrupt
		exit 2
	end

	def check_support_binaries
		["PassengerWatchdog", "PassengerHelperAgent", "PassengerLoggingAgent"].each do |exe|
			puts "     Checking whether the downloaded #{exe.sub(/^Passenger/, '')} binary is usable"
			output = `env LD_BIND_NOW=1 DYLD_BIND_AT_LAUNCH=1 ./agents/#{exe} --test-binary 1`
			if !$? || $?.exitstatus != 0 || output != "PASS\n"
				@stderr.puts "      --> Not usable, will compile from source"
				return false
			end
		end
		puts "     All good"
		return true
	end

	def download_nginx_binary
		return false if !should_download_binaries?

		puts " --> Downloading web helper for your platform"
		basename = "webhelper-#{@nginx_version}-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
		tarball  = "#{@working_dir}/#{basename}"
		if !download_support_file(basename, tarball)
			puts "     No binary is available for your platform. Will compile it from source."
			return false
		end

		FileUtils.mkdir_p(@nginx_dir)
		Dir.mkdir("#{@working_dir}/nginx")
		Dir.chdir("#{@working_dir}/nginx") do
			result = extract_tarball(tarball)
			if !result
				@stderr.puts " *** Error: cannot extract tarball"
				return false
			end
			if check_nginx_binary
				if system("mv '#{@working_dir}/nginx'/* '#{@nginx_dir}'/")
					return true
				else
					@stderr.puts " *** Error: could not move extracted web helper binary to the right directory"
					return false
				end
			else
				return false
			end
		end
	rescue Interrupt
		exit 2
	end

	def check_nginx_binary
		puts "     Checking whether the downloaded binary is usable"
		output = `env LD_BIND_NOW=1 DYLD_BIND_AT_LAUNCH=1 ./PassengerWebHelper -v 2>&1`
		if $? && $?.exitstatus == 0 && output =~ /nginx version:/
			puts "     All good"
			return true
		else
			@stderr.puts "      --> Not usable, will compile from source"
			return false
		end
	end

	def download_and_extract_nginx_sources
		begin_progress_bar
		puts "Downloading web helper source code..."
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
					show_progress(progress / total * 0.1, 1.0, 1, 1, "Extracting web helper source...")
				end
			rescue Exception
				puts
				raise
			end
			if result
				rename_nginx_proctitle("#{@working_dir}/#{nginx_sources_name}")
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

	def rename_nginx_proctitle(source_dir)
		filename = "#{source_dir}/src/os/unix/ngx_setproctitle.c"
		if File.exist?(filename)
			source = File.open(filename, "r") { |f| f.read }
			source.gsub!('"nginx: "', '"PassengerWebHelper: "')
			File.open(filename, "w") { |f| f.write(source) }
		end
	end

	def compile_support_binaries
		begin_progress_bar
		show_progress(0, 1, 1, 1, "Preparing #{PROGRAM_NAME}...")
		Dir.chdir(PhusionPassenger.build_system_dir) do
			args = "nginx_without_native_support" +
				" CACHING=false" +
				" OUTPUT_DIR='#{@support_dir}'"
			begin
				run_rake_task!(args) do |progress, total|
					show_progress(progress, total, 1, 1, "Compiling #{PROGRAM_NAME}...")
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
		puts
	end
	
	def check_nginx_module_sources_available
		if PhusionPassenger.natively_packaged? && !File.exist?(PhusionPassenger.nginx_module_source_dir)
			case PhusionPassenger.native_packaging_method
			when "deb"
				command = "sudo sh -c 'apt-get update && apt-get install #{DEB_DEV_PACKAGE}'"
			when "rpm"
				command = "sudo yum install #{RPM_DEV_PACKAGE}-#{VERSION_STRING}"
			end
			if command
				if STDIN.tty?
					puts " --> Installing #{PhusionPassenger::PROGRAM_NAME} web helper sources"
					puts "     Running: #{command}"
					if system(command)
						return true
					else
						puts "     <red>*** Command failed: #{command}</red>"
						return false
					end
				else
					puts " --> #{PhusionPassenger::PROGRAM_NAME} web helper sources not installed"
					puts "     Please install them first: #{command}"
					return false
				end
			else
				puts " --> #{PhusionPassenger::PROGRAM_NAME} web helper sources not installed"
				puts "     <red>Please ask your operating system vendor how to install these.</red>"
				return false
			end
		else
			return true
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
			puts "<banner>Installing #{PROGRAM_NAME} Standalone...</banner>"
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

	def download_support_file(name, output)
		logger = Logger.new(STDOUT)
		logger.level = Logger::WARN
		logger.formatter = proc do |severity, datetime, progname, msg|
			msg.gsub(/^/, "     ") + "\n"
		end

		if @binaries_url_root.is_a?(String)
			sites = [{ :url => @binaries_url_root }]
		else
			sites = @binaries_url_root
		end
		sites.each_with_index do |site, i|
			if real_download_support_file(site, name, output, logger)
				logger.warn "Download OK!" if i > 0
				return true
			elsif i != sites.size - 1
				logger.warn "Trying next mirror..."
			end
		end
		return false
	end

	def real_download_support_file(site, name, output, logger)
		url = "#{site[:url]}/#{VERSION_STRING}/#{name}"
		return download(url, output,
			:cacert => site[:cacert],
			:logger => logger,
			:use_cache => true)
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
		PhusionPassenger.require_passenger_lib 'platform_info/compiler'
		Dir.chdir(source_dir) do
			shell = PlatformInfo.find_command('bash') || "sh"
			command = ""
			lib_dir = "#{@lib_dir}/common/libpassenger_common"
			nginx_libs = COMMON_LIBRARY.only(*NGINX_LIBS_SELECTOR).
				set_output_dir(lib_dir).
				link_objects_as_string
			command << "env PASSENGER_INCLUDEDIR='#{PhusionPassenger.include_dir}' " <<
				"PASSENGER_LIBS='#{nginx_libs} #{lib_dir}/../libboost_oxt.a' "
			# RPM thinks it's being smart by scanning binaries for
			# paths and refusing to create package if it detects any
			# hardcoded thats that point to /usr or other important
			# locations. For Phusion Passenger Standalone we do not
			# care at all what the Nginx configured prefix is because
			# we pass it its resource locations during runtime, so
			# work around the problem by configure Nginx with prefix
			# /tmp.
			command << "#{shell} ./configure --prefix=/tmp " <<
				"#{STANDALONE_NGINX_CONFIGURE_OPTIONS} " <<
				"'--add-module=#{PhusionPassenger.nginx_module_source_dir}'"
			run_command_with_throbber(command, "Preparing web helper...") do |status_text|
				yield(0, 1, status_text)
			end
			
			backlog = ""

			# Capture and index the `make --dry-run` output for
			# progress determination.
			total_lines = 0
			dry_run_output = {}
			`#{PlatformInfo.gnu_make} --dry-run`.split("\n").each do |line|
				total_lines += 1
				dry_run_output[line] = true
			end

			IO.popen("#{PlatformInfo.gnu_make} 2>&1", "r") do |io|
				progress = 1
				while !io.eof?
					line = io.readline
					backlog << line
					# If the output is part of what we saw when dry-running,
					# then increase progress bar. Otherwise it could be compiler
					# warnings or something, so ignore those.
					if dry_run_output[line.chomp]
						yield(progress, total_lines, "Compiling web helper...")
						progress += 1
					end
				end
			end
			if $?.exitstatus != 0
				@stderr.puts
				@stderr.puts "*** ERROR: unable to compile web helper."
				@stderr.puts backlog
				exit 1
			end
			
			yield(1, 1, 'Copying files...')
			if !system("cp -pR objs/nginx '#{@nginx_dir}/PassengerWebHelper'")
				@stderr.puts
				@stderr.puts "*** ERROR: unable to copy web helper binary."
				exit 1
			end
			if !strip_binary("#{@nginx_dir}/PassengerWebHelper")
				@stderr.puts
				@stderr.puts "*** ERROR: unable to strip debugging symbols from the web helper binary."
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

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

require 'rbconfig'
require 'phusion_passenger/platform_info'

module PhusionPassenger

module PlatformInfo
	# Store original $GEM_HOME value so that even if the app customizes
	# $GEM_HOME we can still work with the original value.
	gem_home = ENV['GEM_HOME']
	if gem_home
		gem_home = gem_home.strip.freeze
		gem_home = nil if gem_home.empty?
	end
	GEM_HOME = gem_home
	
	if defined?(::RUBY_ENGINE)
		RUBY_ENGINE = ::RUBY_ENGINE
	else
		RUBY_ENGINE = "ruby"
	end
	
	# Returns correct command for invoking the current Ruby interpreter.
	# In case of RVM this function will return the path to the RVM wrapper script
	# that executes the current Ruby interpreter in the currently active gem set.
	def self.ruby_command
		if in_rvm?
			name = rvm_ruby_string
			dir = rvm_path
			if name && dir
				filename = "#{dir}/wrappers/#{name}/ruby"
				if File.exist?(filename)
					contents = File.open(filename, 'rb') do |f|
						f.read
					end
					# Old wrapper scripts reference $HOME which causes
					# things to blow up when run by a different user.
					if contents.include?("$HOME")
						filename = nil
					end
				else
					filename = nil
				end
				if filename
					return filename
				else
					STDERR.puts "Your RVM wrapper scripts are too old. Please " +
						"update them first by running 'rvm update --head && " +
						"rvm reload && rvm repair all'."
					exit 1
				end
			else
				# Something's wrong with the user's RVM installation.
				# Raise an error so that the user knows this instead of
				# having things fail randomly later on.
				# 'name' is guaranteed to be non-nil because rvm_ruby_string
				# already raises an exception on error.
				STDERR.puts "Your RVM installation appears to be broken: the RVM " +
					"path cannot be found. Please fix your RVM installation " +
					"or contact the RVM developers for support."
				exit 1
			end
		else
			return ruby_executable
		end
	end
	memoize :ruby_command
	
	# Returns the full path to the current Ruby interpreter's executable file.
	# This might not be the actual correct command to use for invoking the Ruby
	# interpreter; use ruby_command instead.
	def self.ruby_executable
		@@ruby_executable ||=
			Config::CONFIG['bindir'] + '/' + Config::CONFIG['RUBY_INSTALL_NAME'] + Config::CONFIG['EXEEXT']
	end
	
	# Returns whether the Ruby interpreter supports process forking.
	def self.ruby_supports_fork?
		# MRI >= 1.9.2's respond_to? returns false for methods
		# that are not implemented.
		return Process.respond_to?(:fork) &&
			RUBY_ENGINE != "jruby" &&
			RUBY_ENGINE != "macruby" &&
			Config::CONFIG['target_os'] !~ /mswin|windows|mingw/
	end
	
	# Returns the correct 'gem' command for this Ruby interpreter.
	def self.gem_command
		return locate_ruby_tool('gem')
	end
	memoize :gem_command
	
	# Returns the absolute path to the Rake executable that
	# belongs to the current Ruby interpreter. Returns nil if it
	# doesn't exist.
	#
	# The return value may not be the actual correct invocation
	# for Rake. Use rake_command for that.
	def self.rake
		return locate_ruby_tool('rake')
	end
	memoize :rake
	
	# Returns the correct command string for invoking the Rake executable
	# that belongs to the current Ruby interpreter. Returns nil if Rake is
	# not found.
	def self.rake_command
		filename = rake
		# If the Rake executable is a Ruby program then we need to run
		# it in the correct Ruby interpreter just in case Rake doesn't
		# have the correct shebang line; we don't want a totally different
		# Ruby than the current one to be invoked.
		if filename && is_ruby_program?(filename)
			return "#{ruby_command} #{filename}"
		else
			# If it's not a Ruby program then it's probably a wrapper
			# script as is the case with e.g. RVM (~/.rvm/wrappers).
			return filename
		end
	end
	memoize :rake_command
	
	# Returns the absolute path to the RSpec runner program that
	# belongs to the current Ruby interpreter. Returns nil if it
	# doesn't exist.
	def self.rspec
		return locate_ruby_tool('spec')
	end
	memoize :rspec
	
	# Returns whether the current Ruby interpreter is managed by RVM.
	def self.in_rvm?
		bindir = Config::CONFIG['bindir']
		return bindir.include?('/.rvm/') || bindir.include?('/rvm/')
	end
	
	# If the current Ruby interpreter is managed by RVM, returns the
	# directory in which RVM places its working files. Otherwise returns
	# nil.
	def self.rvm_path
		if in_rvm?
			[ENV['rvm_path'], "~/.rvm", "/usr/local/rvm"].each do |path|
				next if path.nil?
				path = File.expand_path(path)
				return path if File.directory?(path)
			end
			# Failure to locate the RVM path is probably caused by the
			# user customizing $rvm_path. Older RVM versions don't
			# export $rvm_path, making us unable to detect its value.
			STDERR.puts "Unable to locate the RVM path. Your RVM installation " +
				"is probably too old. Please update it with " +
				"'rvm update --head && rvm reload && rvm repair all'."
			exit 1
		else
			return nil
		end
	end
	memoize :rvm_path
	
	# If the current Ruby interpreter is managed by RVM, returns the
	# RVM name which identifies the current Ruby interpreter plus the
	# currently active gemset, e.g. something like this:
	# "ruby-1.9.2-p0@mygemset"
	#
	# Returns nil otherwise.
	def self.rvm_ruby_string
		if in_rvm?
			# RVM used to export the necessary information through
			# environment variables, but doesn't always do that anymore
			# in the latest versions in order to fight env var pollution.
			# Scanning $LOAD_PATH seems to be the only way to obtain
			# the information.
			
			# Getting the RVM name of the Ruby interpreter ("ruby-1.9.2")
			# isn't so hard, we can extract it from the #ruby_executable
			# string. Getting the gemset name is a bit harder, so let's
			# try various strategies...
			
			# $GEM_HOME usually contains the gem set name.
			if GEM_HOME && GEM_HOME.include?("rvm/gems/")
				return File.basename(GEM_HOME)
			end
			
			# User somehow managed to nuke $GEM_HOME. Extract info
			# from $LOAD_PATH.
			matching_path = $LOAD_PATH.find_all do |item|
				item.include?("rvm/gems/")
			end
			if matching_path
				subpath = matching_path.to_s.gsub(/^.*rvm\/gems\//, '')
				result = subpath.split('/').first
				return result if result
			end
			
			# On Ruby 1.9, $LOAD_PATH does not contain any gem paths until
			# at least one gem has been required so the above can fail.
			# We're out of options now, we can't detect the gem set.
			# Raise an exception so that the user knows what's going on
			# instead of having things fail in obscure ways later.
			STDERR.puts "Unable to autodetect the currently active RVM gem " +
				"set name. Please contact this program's author for support."
			exit 1
		end
		return nil
	end
	memoize :rvm_ruby_string
	
	# Returns either 'sudo' or 'rvmsudo' depending on whether the current
	# Ruby interpreter is managed by RVM.
	def self.ruby_sudo_command
		if in_rvm?
			return "rvmsudo"
		else
			return "sudo"
		end
	end
	
	# Locates a Ruby tool command, e.g. 'gem', 'rake', 'bundle', etc. Instead of
	# naively looking in $PATH, this function uses a variety of search heuristics
	# to find the command that's really associated with the current Ruby interpreter.
	# It should never locate a command that's actually associated with a different
	# Ruby interpreter.
	# Returns nil when nothing's found.
	def self.locate_ruby_tool(name)
		result = locate_ruby_tool_by_basename(name)
		if !result
			exeext = Config::CONFIG['EXEEXT']
			exeext = nil if exeext.empty?
			if exeext
				result = locate_ruby_tool_by_basename("#{name}#{exeext}")
			end
			if !result
				result = locate_ruby_tool_by_basename(transform_according_to_ruby_exec_format(name))
			end
			if !result && exeext
				result = locate_ruby_tool_by_basename(transform_according_to_ruby_exec_format(name) + exeext)
			end
		end
		return result
	end

private
	def self.locate_ruby_tool_by_basename(name)
		if RUBY_PLATFORM =~ /darwin/ &&
		   ruby_command =~ %r(\A/System/Library/Frameworks/Ruby.framework/Versions/.*?/usr/bin/ruby\Z)
			# On OS X we must look for Ruby binaries in /usr/bin.
			# RubyGems puts executables (e.g. 'rake') in there, not in
			# /System/Libraries/(...)/bin.
			filename = "/usr/bin/#{name}"
		else
			filename = File.dirname(ruby_command) + "/#{name}"
		end

		if !File.file?(filename) || !File.executable?(filename)
			# RubyGems might put binaries in a directory other
			# than Ruby's bindir. Debian packaged RubyGems and
			# DebGem packaged RubyGems are the prime examples.
			begin
				require 'rubygems' unless defined?(Gem)
				filename = Gem.bindir + "/#{name}"
			rescue LoadError
				filename = nil
			end
		end

		if !filename || !File.file?(filename) || !File.executable?(filename)
			# Looks like it's not in the RubyGems bindir. Search in $PATH, but
			# be very careful about this because whatever we find might belong
			# to a different Ruby interpreter than the current one.
			ENV['PATH'].split(':').each do |dir|
				filename = "#{dir}/#{name}"
				if File.file?(filename) && File.executable?(filename)
					shebang = File.open(filename, 'rb') do |f|
						f.readline.strip
					end
					if shebang == "#!#{ruby_command}"
						# Looks good.
						break
					end
				end

				# Not found. Try next path.
				filename = nil
			end
		end

		filename
	end
	private_class_method :locate_ruby_tool_by_basename
	
	def self.is_ruby_program?(filename)
		File.open(filename, 'rb') do |f|
			return f.readline =~ /ruby/
		end
	rescue EOFError
		return false
	end
	private_class_method :is_ruby_program?
	
	# Deduce Ruby's --program-prefix and --program-suffix from its install name
	# and transforms the given input name accordingly.
	#
	#   transform_according_to_ruby_exec_format("rake")    => "jrake", "rake1.8", etc
	def self.transform_according_to_ruby_exec_format(name)
		install_name = Config::CONFIG['RUBY_INSTALL_NAME']
		if install_name.include?('ruby')
			format = install_name.sub('ruby', '%s')
			return sprintf(format, name)
		else
			return name
		end
	end
	private_class_method :transform_according_to_ruby_exec_format
end

end # module PhusionPassenger

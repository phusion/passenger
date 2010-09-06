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
	if defined?(::RUBY_ENGINE)
		RUBY_ENGINE = ::RUBY_ENGINE
	else
		RUBY_ENGINE = "ruby"
	end
	
	# Returns correct command for invoking the current Ruby interpreter.
	# In case of RVM this function will return the path to the RVM wrapper script
	# that executes the current Ruby interpreter in the currently active gem set.
	def self.ruby_command
		@@ruby_command ||= begin
			filename = ruby_executable
			if filename =~ %r{(.*)/.rvm/rubies/(.+?)/bin/(.+)}
				home = $1
				name = $2
				exename = $3
				if !ENV['rvm_gemset_name'].to_s.empty?
					name << "@#{ENV['rvm_gemset_name']}"
				end
				new_filename = "#{home}/.rvm/wrappers/#{name}/#{exename}"
				if File.exist?(new_filename)
					filename = new_filename
				end
			end
			filename
		end
	end
	
	# Returns the full path to the current Ruby interpreter's executable file.
	# This might not be the actual correct command to use for invoking the Ruby
	# interpreter; use ruby_command instead.
	def self.ruby_executable
		@@ruby_executable ||=
			Config::CONFIG['bindir'] + '/' + Config::CONFIG['RUBY_INSTALL_NAME'] + Config::CONFIG['EXEEXT']
	end
	
	# Returns whether the Ruby interpreter supports process forking.
	def self.fork_supported?
		# MRI >= 1.9.2's respond_to? returns false for methods
		# that are not implemented.
		return Process.respond_to?(:fork) &&
			RUBY_ENGINE != "jruby" &&
			RUBY_ENGINE != "macruby" &&
			Config::CONFIG['target_os'] !~ /mswin|windows|mingw/
	end
	
	# Returns the correct 'gem' command for this Ruby interpreter.
	def self.gem_command
		return locate_ruby_executable('gem')
	end
	memoize :gem_command
	
	# Returns the absolute path to the Rake executable that
	# belongs to the current Ruby interpreter. Returns nil if it
	# doesn't exist.
	#
	# The return value may not be the actual correct invocation
	# for Rake. Use rake_command for that.
	def self.rake
		return locate_ruby_executable('rake')
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
		return locate_ruby_executable('spec')
	end
	memoize :rspec
	
	# Returns whether the current Ruby interpreter is managed by RVM.
	def self.in_rvm?
		return Config::CONFIG['bindir'].include?('/.rvm/')
	end
	
	# Returns either 'sudo' or 'rvmsudo' depending on whether the current
	# Ruby interpreter is managed by RVM.
	def self.ruby_sudo_command
		if in_rvm?
			return "rvmsudo"
		else
			return "sudo"
		end
	end
	
	# Locate a Ruby tool command, e.g. 'gem', 'rake', 'bundle', etc. Instead of
	# naively looking in $PATH, this function uses a variety of search heuristics
	# to find the command that's really associated with the current Ruby interpreter.
	# It should never locate a command that's actually associated with a different
	# Ruby interpreter.
	def self.locate_ruby_executable(name)
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

private
	def self.is_ruby_program?(filename)
		File.open(filename, 'rb') do |f|
			return f.readline =~ /ruby/
		end
	rescue EOFError
		return false
	end
	private_class_method :is_ruby_program?
end

end # module PhusionPassenger

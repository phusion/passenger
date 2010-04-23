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

# Indicates that there is no Ruby on Rails version installed that satisfies
# a given Ruby on Rails Gem version specification.
class VersionNotFound < StandardError
	attr_reader :gem_version_spec
	
	# - +message+: The exception message.
	# - +gem_version_spec+: The Ruby on Rails Gem version specification that caused this error.
	def initialize(message, gem_version_spec)
		super(message)
		@gem_version_spec = gem_version_spec
	end
end

# An abstract base class for AppInitError and FrameworkInitError. This represents
# the failure when initializing something.
class InitializationError < StandardError
	# The exception that caused initialization to fail. This may be nil.
	attr_accessor :child_exception

	# Create a new InitializationError. +message+ is the error message,
	# and +child_exception+ is the exception that caused initialization
	# to fail.
	def initialize(message, child_exception = nil)
		super(message)
		@child_exception = child_exception
	end
end

# Raised when Rack::ApplicationSpawner, ClassicRails::ApplicationSpawner,
# ClassicRails::FrameworkSpawner or SpawnManager was unable to spawn an application,
# because the application either threw an exception or called exit.
#
# If the application called exit, then +child_exception+ is an instance of
# +SystemExit+.
class AppInitError < InitializationError
	# The application type, e.g. "rails" or "rack".
	attr_accessor :app_type
	# Any messages printed to stderr before the failure. May be nil.
	attr_accessor :stderr
	
	def initialize(message, child_exception = nil, app_type = "rails", stderr = nil)
		super(message, child_exception)
		@app_type = app_type
		@stderr = stderr
	end
end

# Raised when ClassicRails::FrameworkSpawner or ClassicRails::SpawnManager was unable to load a
# version of the Ruby on Rails framework. The +child_exception+ attribute is guaranteed
# non-nil.
class FrameworkInitError < InitializationError
	attr_reader :vendor
	attr_reader :version
	
	def initialize(message, child_exception, options)
		super(message, child_exception)
		if options[:vendor]
			@vendor = options[:vendor]
		else
			@version = options[:version]
		end
	end
end

class UnknownError < StandardError
	attr_accessor :real_class_name
	
	def initialize(message, class_name, backtrace)
		super("#{message} (#{class_name})")
		set_backtrace(backtrace)
		@real_class_name = class_name
	end
end

class InvalidPath < StandardError
end

end # module PhusionPassenger

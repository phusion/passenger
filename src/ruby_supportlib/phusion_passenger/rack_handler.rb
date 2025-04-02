# frozen_string_literal: true

#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2016-2025 Asynchronous B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Asynchronous B.V.
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

## Magic comment: begin bootstrap ##
libdir = File.expand_path('..', File.dirname(__FILE__))
$LOAD_PATH.unshift(libdir)
begin
  require 'rubygems'
rescue LoadError
end
require 'phusion_passenger'
require_relative 'rack/handler'
## Magic comment: end bootstrap ##

PhusionPassenger.locate_directories

require 'rbconfig'
require 'rackup' if Gem::Version.new(RUBY_VERSION) >= Gem::Version.new('2.4.0')

# Rackup was removed in Rack 3, it is now a separate gem
if Object.const_defined?(:Rackup) && ::Rackup.const_defined?(:Handler)
  module Rackup
    module Handler
      module PhusionPassenger
        class << self
          include ::PhusionPassenger::Rack::Handler
        end
      end

      def self.default(options = {})
        ::Rackup::Handler::PhusionPassenger
      end

      register :passenger, PhusionPassenger
    end
  end
elsif Object.const_defined?(:Rack) && ::Rack.release < '3'
  module Rack
    module Handler
      module PhusionPassenger
        class << self
          include ::PhusionPassenger::Rack::Handler
        end
      end
      def self.default(options = {})
        ::Rack::Handler::PhusionPassenger
      end
    end
  end
  ::Rack::Handler.register(:passenger, ::Rack::Handler::PhusionPassenger)
else
  raise 'Rack 3 must be used with the Rackup gem'
end

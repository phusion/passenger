# frozen_string_literal: true

#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2024 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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
  module Rack
    module Handler
      def run(app, options = {})
        result = system(ruby_executable, '-S', find_passenger_standalone,
                        'start', *build_args(options))
        if !result
          raise "Error starting Passenger"
        end
      end

      def environment
        ENV['RAILS_ENV'] || 'development'
      end

      def to_s
        'Passenger application server'
      end

      private
      def build_args(options)
        args = ['-e', environment]
        args << '-p' << options[:Port].to_s if options[:Port]
        args << '-a' << options[:Host].to_s if options[:Host]
        args << '-R' << options[:config].to_s if options[:config]
        args
      end

      def rb_config
        if defined?(::RbConfig)
          ::RbConfig::CONFIG
        else
          ::Config::CONFIG
        end
      end

      def ruby_executable
        @ruby_executable ||= rb_config['bindir'] + '/' +
                             rb_config['RUBY_INSTALL_NAME'] + rb_config['EXEEXT']
      end

      def find_passenger_standalone
        ::File.join(::PhusionPassenger.bin_dir, 'passenger')
      end
    end
  end
end

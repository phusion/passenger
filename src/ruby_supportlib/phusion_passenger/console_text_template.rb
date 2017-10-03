# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

require 'erb'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'

module PhusionPassenger

  class ConsoleTextTemplate
    def initialize(input, options = {})
      @buffer = ''
      if input[:file]
        filename = "#{PhusionPassenger.resources_dir}/templates/#{input[:file]}.txt.erb"
        data = File.open(filename, 'r:utf-8') do |f|
          f.read
        end
      else
        data = input[:text]
      end
      @colors = options[:colors] || AnsiColors.new
      @template = ERB.new(@colors.ansi_colorize(data),
        nil, '-', '@buffer')
      @template.filename = filename if filename
      options.each_pair do |name, value|
        self[name] = value
      end
    end

    def []=(name, value)
      instance_variable_set("@#{name}".to_sym, value)
      return self
    end

    def result
      return @template.result(binding)
    end
  end

end # module PhusionPassenger

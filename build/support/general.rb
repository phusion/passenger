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

class CxxCodeTemplateRenderer
  def initialize(filename)
    if !defined?(CxxCodeBuilder)
      require_build_system_file('support/vendor/cxxcodebuilder/lib/cxxcodebuilder')
    end
    code = File.open(filename, 'rb') do |f|
      f.read
    end
    @builder = CxxCodeBuilder::Builder.new
    @builder.instance_eval(code, filename)
  end

  def render
    @builder.to_s
  end

  def render_to(filename)
    puts "Creating #{filename}"
    text = render
    # When packaging, some timestamps may be modified. The user may not
    # have write access to the source root (for example, when Passenger
    # Standalone is compiling its runtime), so we only write to the file
    # when necessary.
    if !File.exist?(filename) || File.writable?(filename) || File.read(filename) != text
      File.open(filename, 'w') do |f|
        f.write(text)
      end
    end
  end
end

class Pathname
  if !method_defined?(:/)
    def /(other)
      self + other.to_s
    end
  end
end

def string_option(name, default_value = nil)
  value = ENV[name]
  if value.nil? || value.empty?
    if default_value.respond_to?(:call)
      default_value.call
    else
      default_value
    end
  else
    value
  end
end

def pathname_option(name, default_value)
  Pathname.new(string_option(name, default_value))
end

def compiler_flag_option(name, default_value = '')
  string_option(name, default_value).gsub("\n", " ")
end

def boolean_option(name, default_value = false)
  value = ENV[name]
  if value.nil? || value.empty?
    default_value
  else
    value == "yes" || value == "on" || value == "true" || value == "1"
  end
end

def maybe_wrap_in_ccache(command)
  if boolean_option('USE_CCACHE', false) && command !~ /^ccache /
    "ccache #{command}"
  else
    command
  end
end

def copyright_header_for(filename)
  contents = File.open(filename, 'rb') do |f|
    f.read
  end
  contents =~ /\A(#.+?)\n\n/m
  $1.gsub(/^# */, '')
end

def ensure_target_directory_exists(target)
  dir = File.dirname(target)
  if !File.exist?(dir)
    sh "mkdir -p #{dir}"
  end
end

def shesc(path)
  Shellwords.escape(path)
end

LET_CACHE = {}

def let(name)
  name = name.to_sym
  Kernel.send(:define_method, name) do
    if LET_CACHE.key?(name)
      LET_CACHE[name]
    else
      LET_CACHE[name] = yield
    end
  end
end

def maybe_eval_lambda(lambda_or_value)
  if lambda_or_value.respond_to?(:call)
    lambda_or_value.call
  else
    lambda_or_value
  end
end


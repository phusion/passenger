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

# Rake functions for compiling/linking C++ stuff.

def run_compiler(*command)
  PhusionPassenger.require_passenger_lib 'utils/ansi_colors' if !defined?(PhusionPassenger::Utils::AnsiColors)
  show_command = command.join(' ')
  puts show_command
  if !system(*command)
    colors = PhusionPassenger::Utils::AnsiColors.new
    if $? && $?.exitstatus == 4
      # This probably means the compiler ran out of memory.
      msg = "<b>" +
            "-----------------------------------------------\n" +
            "Your compiler failed with the exit status 4. This " +
            "probably means that it ran out of memory. To solve " +
            "this problem, try increasing your swap space: " +
            "https://www.digitalocean.com/community/articles/how-to-add-swap-on-ubuntu-12-04" +
            "</b>"
      fail(colors.ansi_colorize(msg))
    elsif $? && $?.termsig == 9
      msg = "<b>" +
            "-----------------------------------------------\n" +
            "Your compiler was killed by the operating system. This " +
            "probably means that it ran out of memory. To solve " +
            "this problem, try increasing your swap space: " +
            "https://www.digitalocean.com/community/articles/how-to-add-swap-on-ubuntu-12-04" +
            "</b>"
      fail(colors.ansi_colorize(msg))
    else
      fail "Command failed with status (#{$? ? $?.exitstatus : 1}): [#{show_command}]"
    end
  end
end

def build_compiler_flags_from_options_or_flags(options_or_flags)
  if options_or_flags.is_a?(Hash)
    result = []
    options = options_or_flags

    (options[:include_paths] || []).each do |path|
      result << "-I#{path}"
    end

    if flags = options[:flags]
      result.concat([flags].flatten)
    end

    result.flatten.reject{ |x| x.nil? || x.empty? }.join(" ")
  elsif options_or_flags.is_a?(String)
    options_or_flags
  elsif options_or_flags.respond_to?(:call)
    build_compiler_flags_from_options_or_flags(options_or_flags.call)
  elsif options_or_flags.nil?
    ""
  else
    raise ArgumentError, "Invalid argument type: #{options_or_flags.inspect}"
  end
end

def generate_compilation_task_dependencies(source, options = nil)
  result = [source]
  if dependencies = CXX_DEPENDENCY_MAP[source]
    result.concat(dependencies)
  end
  options = maybe_eval_lambda(options)
  if options && options[:deps]
    result.concat([options[:deps]].flatten.compact)
  end
  result
end

def compile_c(object, source, options_or_flags = nil)
  flags = build_compiler_flags_from_options_or_flags(options_or_flags)
  ensure_target_directory_exists(object)
  run_compiler("#{cc} -o #{object} #{EXTRA_PRE_CFLAGS} #{flags} #{extra_cflags} -c #{source}")
end

def compile_cxx(object, source, options_or_flags = nil)
  flags = build_compiler_flags_from_options_or_flags(options_or_flags)
  ensure_target_directory_exists(object)
  run_compiler("#{cxx} -o #{object} #{EXTRA_PRE_CXXFLAGS} #{flags} #{extra_cxxflags} -c #{source}")
end

def create_c_executable(target, objects, options_or_flags = nil)
  objects = [objects].flatten.join(" ")
  flags = build_compiler_flags_from_options_or_flags(options_or_flags)
  ensure_target_directory_exists(target)
  run_compiler("#{cc} -o #{target} #{objects} #{EXTRA_PRE_C_LDFLAGS} #{flags} #{extra_c_ldflags}")
end

def create_cxx_executable(target, objects, options_or_flags = nil)
  objects = [objects].flatten.join(" ")
  flags = build_compiler_flags_from_options_or_flags(options_or_flags)
  ensure_target_directory_exists(target)
  run_compiler("#{cxx} -o #{target} #{objects} #{EXTRA_PRE_CXX_LDFLAGS} #{flags} #{extra_cxx_ldflags}")
end

def create_static_library(target, objects)
  # On OS X, 'ar cru' will sometimes fail with an obscure error:
  #
  #   ar: foo.a is a fat file (use libtool(1) or lipo(1) and ar(1) on it)
  #   ar: foo.a: Inappropriate file type or format
  #
  # So here we delete the ar file before creating it, which bypasses this problem.
  objects = [objects].flatten.join(" ")
  ensure_target_directory_exists(target)
  sh "rm -rf #{target}"
  sh "ar cru #{target} #{objects}"
  sh "ranlib #{target}"
end

def create_shared_library(target, objects, options_or_flags = nil)
  if PlatformInfo.os_name_simple == "macosx"
    shlib_flag = "-flat_namespace -bundle -undefined dynamic_lookup"
  else
    shlib_flag = "-shared"
  end
  if PhusionPassenger::PlatformInfo.cxx_is_sun_studio?
    fPIC = "-KPIC"
  else
    fPIC = "-fPIC"
  end
  objects = [objects].flatten.join(" ")
  flags = build_compiler_flags_from_options_or_flags(options_or_flags)
  ensure_target_directory_exists(target)
  run_compiler("#{cxx} #{shlib_flag} #{objects} #{fPIC} -o #{target} #{flags}")
end

def define_c_object_compilation_task(object, source, options_or_flags = nil)
  options = options_or_flags if options_or_flags.is_a?(Hash) || options_or_flags.respond_to?(:call)
  file(object => generate_compilation_task_dependencies(source, options)) do
    compile_c(object, source, options_or_flags)
  end
end

def define_cxx_object_compilation_task(object, source, options_or_flags = nil)
  options = options_or_flags if options_or_flags.is_a?(Hash) || options_or_flags.respond_to?(:call)
  file(object => generate_compilation_task_dependencies(source, options)) do
    compile_cxx(object, source, options_or_flags)
  end
end

def define_c_or_cxx_object_compilation_task(object, source, options_or_flags = nil)
  if source =~ /\.c$/
    define_c_object_compilation_task(object, source, options_or_flags)
  else
    define_cxx_object_compilation_task(object, source, options_or_flags)
  end
end

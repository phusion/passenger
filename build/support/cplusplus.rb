#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2026 Asynchronous B.V.
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

# Rake functions for compiling/linking C++ stuff.

def run_compiler(*command, environment: {})
  environment = maybe_eval_lambda(environment) || {}
  environment = environment.transform_keys(&:to_s).transform_values(&:to_s)
  environment_display = environment.map do |name, value|
    "#{name}=#{Shellwords.escape(value)}"
  end
  show_command = (environment_display + command).join(' ')
  puts show_command
  if !system(environment, *command)
    colors = PhusionPassenger::Utils::AnsiColors.new
    if $? && $?.exitstatus == 4
      # This probably means the compiler ran out of memory.
      msg = "<b>" \
            "-----------------------------------------------\n" \
            "Your compiler failed with the exit status 4. This " \
            "probably means that it ran out of memory. To solve " \
            "this problem, try increasing your swap space: " \
            "https://www.digitalocean.com/community/articles/how-to-add-swap-on-ubuntu-12-04" \
            "</b>"
      fail(colors.ansi_colorize(msg))
    elsif $? && $?.termsig == 9
      msg = "<b>" +
            "-----------------------------------------------\n" \
            "Your compiler was killed by the operating system. This " \
            "probably means that it ran out of memory. To solve " \
            "this problem, try increasing your swap space: " \
            "https://www.digitalocean.com/community/articles/how-to-add-swap-on-ubuntu-12-04" \
            "</b>"
      fail(colors.ansi_colorize(msg))
    else
      fail "Command failed with status (#{$? ? $?.exitstatus : 1}): [#{show_command}]"
    end
  end
end

def build_compiler_flags(include_paths: [], flags: [])
  result = []

  (maybe_eval_lambda(include_paths) || []).each do |path|
    result << "-I#{path}"
  end

  result.concat([ maybe_eval_lambda(flags) ].flatten)
  result.flatten.reject { |x| x.nil? || x.empty? }.join(" ")
end

def generate_compilation_task_dependencies(source, deps: [])
  result = [ source ]
  if (dependencies = CXX_DEPENDENCY_MAP[source])
    result.concat(dependencies)
  end
  result.concat([ maybe_eval_lambda(deps) ].flatten.compact)
  result
end

def compile_c(object, source, environment: {}, include_paths: [], flags: [])
  flags = build_compiler_flags(include_paths: include_paths, flags: flags)
  ensure_target_directory_exists(object)
  run_compiler("#{cc} -o #{object} #{EXTRA_PRE_CFLAGS} #{flags} #{extra_cflags} -c #{source}",
    environment: environment)
end

def compile_cxx(object, source, environment: {}, include_paths: [], flags: [])
  flags = build_compiler_flags(include_paths: include_paths, flags: flags)
  ensure_target_directory_exists(object)
  run_compiler("#{cxx} -o #{object} #{EXTRA_PRE_CXXFLAGS} #{flags} #{extra_cxxflags} -c #{source}",
    environment: environment)
end

def create_c_executable(target, objects, environment: {}, flags: [])
  objects = [ objects ].flatten.join(" ")
  flags = build_compiler_flags(flags: flags)
  ensure_target_directory_exists(target)
  run_compiler("#{cc} -o #{target} #{objects} #{EXTRA_PRE_C_LDFLAGS} #{flags} #{extra_c_ldflags}",
    environment: environment)
end

def create_cxx_executable(target, objects, environment: {}, flags: [])
  objects = [ objects ].flatten.join(" ")
  flags = build_compiler_flags(flags: flags)
  ensure_target_directory_exists(target)
  run_compiler("#{cxx} -o #{target} #{objects} #{EXTRA_PRE_CXX_LDFLAGS} #{flags} #{extra_cxx_ldflags}",
    environment: environment)
end

def create_static_library(target, objects)
  # On OS X, 'ar cru' will sometimes fail with an obscure error:
  #
  #   ar: foo.a is a fat file (use libtool(1) or lipo(1) and ar(1) on it)
  #   ar: foo.a: Inappropriate file type or format
  #
  # So here we delete the ar file before creating it, which bypasses this problem.
  objects = [ objects ].flatten.join(" ")
  ensure_target_directory_exists(target)
  sh "rm -rf #{target}"
  sh "ar cru #{target} #{objects}"
  sh "ranlib #{target}"
end

def create_shared_library(target, objects, environment: {}, flags: [])
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
  objects = [ objects ].flatten.join(" ")
  flags = build_compiler_flags(flags: flags)
  ensure_target_directory_exists(target)
  run_compiler("#{cxx} #{shlib_flag} #{objects} #{fPIC} -o #{target} #{flags}",
    environment: environment)
end

def define_c_object_compilation_task(object, source, environment: {}, include_paths: [], flags: [], deps: [])
  file(object => generate_compilation_task_dependencies(source, deps: deps)) do
    compile_c(object, source, environment: environment, include_paths: include_paths, flags: flags)
  end
end

def define_cxx_object_compilation_task(object, source, environment: {}, include_paths: [], flags: [], deps: [])
  file(object => generate_compilation_task_dependencies(source, deps: deps)) do
    compile_cxx(object, source, environment: environment, include_paths: include_paths, flags: flags)
  end
end

def define_c_or_cxx_object_compilation_task(object, source, environment: {}, include_paths: [], flags: [], deps: [])
  if source =~ /\.c$/
    define_c_object_compilation_task(object, source,
      environment: environment, include_paths: include_paths, flags: flags, deps: deps)
  else
    define_cxx_object_compilation_task(object, source,
      environment: environment, include_paths: include_paths, flags: flags, deps: deps)
  end
end

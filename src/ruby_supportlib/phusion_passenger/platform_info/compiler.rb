#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger

  module PlatformInfo
  private
    def self.detect_language_extension(language)
      case language
      when :c
        return "c"
      when :cxx
        return "cpp"
      else
        raise ArgumentError, "Unsupported language #{language.inspect}"
      end
    end
    private_class_method :detect_language_extension

    def self.detect_compiler_type_name(language)
      case language
      when :c
        return "C"
      when :cxx
        return "C++"
      else
        raise ArgumentError, "Unsupported language #{language.inspect}"
      end
    end
    private_class_method :detect_compiler_type_name

    def self.create_compiler_command(language, flags1, flags2, link = false)
      case language
      when :c
        result  = [cc, link ? ENV['EXTRA_PRE_LDFLAGS'] : nil,
          ENV['EXTRA_PRE_CFLAGS'], flags1, flags2, ENV['EXTRA_CFLAGS'],
          ENV['EXTRA_LDFLAGS']]
      when :cxx
        result  = [cxx, link ? ENV['EXTRA_PRE_LDFLAGS'] : nil,
          ENV['EXTRA_PRE_CXXFLAGS'], flags1, flags2, ENV['EXTRA_CXXFLAGS'],
          ENV['EXTRA_LDFLAGS']]
      else
        raise ArgumentError, "Unsupported language #{language.inspect}"
      end
      return result.compact.join(" ").strip
    end
    private_class_method :create_compiler_command

    def self.run_compiler(description, command, source_file, source, capture_output = false)
      if verbose?
        message = "#{description}\n" <<
          "Running: #{command}\n"
        if source.strip.empty?
          message << "Source file is empty."
        else
          message << "Source file contains:\n" <<
            "-------------------------\n" <<
            unindent(source) <<
            "\n-------------------------"
        end
        log(message)
      end
      if capture_output
        begin
          output = `#{command} 2>&1`
          result = $?.exitstatus == 0
        rescue SystemCallError => e
          result = nil
          exec_error_reason = e.message
        end
        log("Output:\n" <<
          "-------------------------\n" <<
          output.to_s <<
          "\n-------------------------")
      elsif verbose?
        result = system(command)
      else
        result = system("(#{command}) >/dev/null 2>/dev/null")
      end
      if result.nil?
        log("Command could not be executed! #{exec_error_reason}".strip)
        return false
      elsif result
        log("Check succeeded")
        if capture_output
          return { :result => true, :output => output }
        else
          return true
        end
      else
        log("Check failed with exit status #{$?.exitstatus}")
        if capture_output == :always
          return { :result => false, :output => output }
        else
          return false
        end
      end
    end
    private_class_method :run_compiler

    def self.cc_or_cxx_supports_feliminate_unused_debug?(language)
      ext = detect_language_extension(language)
      compiler_type_name = detect_compiler_type_name(language)
      create_temp_file("passenger-compile-check.#{ext}") do |filename, f|
        f.close
        begin
          command = create_compiler_command(language,
            "-c '#{filename}' -o '#{filename}.o'",
            '-feliminate-unused-debug-symbols -feliminate-unused-debug-types')
          result = run_compiler("Checking for #{compiler_type_name} compiler '-feliminate-unused-debug-{symbols,types}' support",
            command, filename, '', true)
          return result && result[:output].empty?
        ensure
          File.unlink("#{filename}.o") rescue nil
        end
      end
    end
    private_class_method :cc_or_cxx_supports_feliminate_unused_debug?

    def self.cc_or_cxx_supports_blocks?(language)
      ext = detect_language_extension(language)
      compiler_type_name = detect_compiler_type_name(language)
      command = create_compiler_command(language,"-E -dM",'- </dev/null')
      result = `#{command}`
      return result.include? "__BLOCKS__"
    end
    private_class_method :cc_or_cxx_supports_blocks?

  public
    def self.cc
      return string_env('CC', default_cc)
    end
    memoize :cc

    def self.cxx
      return string_env('CXX', default_cxx)
    end
    memoize :cxx

    def self.default_cc
      # On most platforms, we'll want to use the same compiler as what the rest
      # of the system uses, so that we generate compatible binaries. That's
      # most likely the 'cc' command. We used to use 'gcc' by default.
      #
      # See for example this issue with OS X Mavericks (10.9). They switched from
      # GCC to Clang as the default compiler. Since the Nginx by default uses 'cc'
      # as the compiler, we'll have to do that too. Otherwise we'll get C++ linker
      # errors because Nginx is compiled with Clang while Phusion Passenger is
      # compiled with GCC.
      # https://code.google.com/p/phusion-passenger/issues/detail?id=950
      if PlatformInfo.find_command('cc')
        return 'cc'
      else
        return 'gcc'
      end
    end

    def self.default_cxx
      if PlatformInfo.find_command('c++')
        return 'c++'
      else
        return 'g++'
      end
    end

    def self.cc_is_gcc?
      `#{cc} -v 2>&1` =~ /gcc version/
    end
    memoize :cc_is_gcc?

    def self.cxx_is_gcc?
      `#{cxx} -v 2>&1` =~ /gcc version/
    end
    memoize :cxx_is_gcc?

    def self.cc_is_clang?
      `#{cc} --version 2>&1` =~ /clang( version|-)/
    end
    memoize :cc_is_clang?

    def self.cxx_is_clang?
      `#{cxx} --version 2>&1` =~ /clang( version|-)/
    end
    memoize :cxx_is_clang?

    def self.cc_is_sun_studio?
      `#{cc} -V 2>&1` =~ /Sun C/ || `#{cc} -flags 2>&1` =~ /Sun C/
    end
    memoize :cc_is_sun_studio?

    def self.cxx_is_sun_studio?
      `#{cxx} -V 2>&1` =~ /Sun C/ || `#{cxx} -flags 2>&1` =~ /Sun C/
    end
    memoize :cxx_is_sun_studio?


    # Looks for the given C or C++ header. This works by invoking the compiler and
    # searching in the compiler's header search path. Returns its full filename,
    # or true if this function knows that the header exists but can't find it (e.g.
    # because the compiler cannot tell us what its header search path is).
    # Returns nil if the header cannot be found.
    def self.find_header(header_name, language, flags = nil)
      extension = detect_language_extension(language)
      create_temp_file("passenger-compile-check.#{extension}") do |filename, f|
        source = %Q{
          #include <#{header_name}>
        }
        f.puts(source)
        f.close
        begin
          command = create_compiler_command(language,
            "-v -c '#{filename}' -o '#{filename}.o'",
            flags)
          if result = run_compiler("Checking for #{header_name}", command, filename, source, true)
            result[:output] =~ /^#include <...> search starts here:$(.+?)^End of search list\.$/m
            search_paths = $1.to_s.strip.split("\n").map{ |line| line.strip }
            search_paths.each do |dir|
              if File.file?("#{dir}/#{header_name}")
                return "#{dir}/#{header_name}"
              end
            end
            return true
          else
            return nil
          end
        ensure
          File.unlink("#{filename}.o") rescue nil
        end
      end
    end

    def self.try_compile(description, language, source, flags = nil)
      extension = detect_language_extension(language)
      create_temp_file("passenger-compile-check.#{extension}") do |filename, f|
        f.puts(source)
        f.close
        command = create_compiler_command(language,
          "-c '#{filename}' -o '#{filename}.o'",
          flags)
        return run_compiler(description, command, filename, source)
      end
    end

    # Like try_compile, but designed for checking whether a warning flag is
    # supported. Compilers sometimes do not error out upon encountering an
    # unsupported warning flag, but merely print a warning. This method checks
    # for that too.
    def self.try_compile_with_warning_flag(description, language, source, flags = nil)
      extension = detect_language_extension(language)
      result = nil
      create_temp_file("passenger-compile-check.#{extension}") do |filename, f|
        f.puts(source)
        f.close
        command = create_compiler_command(language,
          "-c '#{filename}' -o '#{filename}.o'",
          flags)
        result = run_compiler(description, command, filename, source, true)
        result = result && result[:result] && result[:output] !~ /unknown warning option/i
      end
      return false if !result

      # For some reason, GCC does not complain about a warning flag
      # not being supported unless the source contains another error. So we
      # check for this.
      create_temp_file("passenger-compile-check.#{extension}") do |filename, f|
        source = %Q{
          void foo() {
            return error;
          }
        }
        f.puts(source)
        f.close
        command = create_compiler_command(language,
          "-c '#{filename}' -o '#{filename}.o'",
          flags)
        result = run_compiler("#{description} (really)", command, filename, source, :always)
      end
      result && !result[:output].include?(flags)
    end

    def self.try_link(description, language, source, flags = nil)
      extension = detect_language_extension(language)
      create_temp_file("passenger-link-check.#{extension}") do |filename, f|
        f.puts(source)
        f.close
        command = create_compiler_command(language,
          "'#{filename}' -o '#{filename}.out'",
          flags, true)
        return run_compiler(description, command, filename, source)
      end
    end

    def self.try_compile_and_run(description, language, source, flags = nil)
      extension = detect_language_extension(language)
      create_temp_file("passenger-run-check.#{extension}", tmpexedir) do |filename, f|
        f.puts(source)
        f.close
        command = create_compiler_command(language,
          "'#{filename}' -o '#{filename}.out'",
          flags, true)
        if run_compiler(description, command, filename, source)
          log("Running #{filename}.out")
          begin
            output = `'#{filename}.out' 2>&1`
          rescue SystemCallError => e
            log("Command failed: #{e}")
            return false
          end
          status = $?.exitstatus
          log("Command exited with status #{status}. Output:\n--------------\n#{output}\n--------------")
          return status == 0
        else
          return false
        end
      end
    end


    # Checks whether the compiler supports "-arch #{arch}".
    def self.compiler_supports_architecture?(arch)
      return try_compile("Checking for C compiler '-arch' support",
        :c, '', "-arch #{arch}")
    end

    def self.cc_supports_visibility_flag?
      return false if os_name_simple == "aix"
      return try_compile("Checking for C compiler '-fvisibility' support",
        :c, '', '-fvisibility=hidden')
    end
    memoize :cc_supports_visibility_flag?, true

    def self.cc_supports_fno_limit_debug_info_flag?
      try_compile_with_warning_flag(
        "Checking for C compiler '-fno-limit-debug-info' support",
        :c, '', '-fno-limit-debug-info')
    end
    memoize :cc_supports_fno_limit_debug_info_flag?

    def self.cxx_supports_visibility_flag?
      return false if os_name_simple == "aix"
      return try_compile("Checking for C++ compiler '-fvisibility' support",
        :cxx, '', '-fvisibility=hidden')
    end
    memoize :cxx_supports_visibility_flag?, true

    def self.cc_supports_wno_attributes_flag?
      return try_compile_with_warning_flag(
        "Checking for C compiler '-Wno-attributes' support",
        :c, '', '-Wno-attributes')
    end
    memoize :cc_supports_wno_attributes_flag?, true

    def self.cxx_supports_wno_attributes_flag?
      return try_compile_with_warning_flag(
        "Checking for C++ compiler '-Wno-attributes' support",
        :cxx, '', '-Wno-attributes')
    end
    memoize :cxx_supports_wno_attributes_flag?, true

    def self.cc_supports_wno_missing_field_initializers_flag?
      return try_compile_with_warning_flag(
        "Checking for C compiler '-Wno-missing-field-initializers' support",
        :c, '', '-Wno-missing-field-initializers')
    end
    memoize :cc_supports_wno_missing_field_initializers_flag?, true

    def self.cxx_supports_wno_missing_field_initializers_flag?
      return try_compile_with_warning_flag(
        "Checking for C++ compiler '-Wno-missing-field-initializers' support",
        :cxx, '', '-Wno-missing-field-initializers')
    end
    memoize :cxx_supports_wno_missing_field_initializers_flag?, true

    def self.cxx_supports_wno_unused_local_typedefs_flag?
      return try_compile_with_warning_flag(
        "Checking for C++ compiler '-Wno-unused-local-typedefs' support",
        :cxx, '', '-Wno-unused-local-typedefs')
    end
    memoize :cxx_supports_wno_unused_local_typedefs_flag?, true

    def self.cxx_supports_wno_format_nonliteral_flag?
      return try_compile_with_warning_flag(
        "Checking for C++ compiler '-Wno-format-nonliteral' support",
        :cxx, '', '-Wno-format-nonliteral')
    end
    memoize :cxx_supports_wno_format_nonliteral_flag?, true

    def self.cxx_supports_fno_limit_debug_info_flag?
      try_compile_with_warning_flag(
        "Checking for C++ compiler '-fno-limit-debug-info' support",
        :cxx, '', '-fno-limit-debug-info')
    end
    memoize :cxx_supports_fno_limit_debug_info_flag?

    def self.cc_supports_no_tls_direct_seg_refs_option?
      return try_compile("Checking for C compiler '-mno-tls-direct-seg-refs' support",
        :c, '', '-mno-tls-direct-seg-refs')
    end
    memoize :cc_supports_no_tls_direct_seg_refs_option?, true

    def self.cxx_supports_no_tls_direct_seg_refs_option?
      return try_compile("Checking for C++ compiler '-mno-tls-direct-seg-refs' support",
        :cxx, '', '-mno-tls-direct-seg-refs')
    end
    memoize :cxx_supports_no_tls_direct_seg_refs_option?, true

    def self.compiler_supports_wno_ambiguous_member_template?
      try_compile_with_warning_flag(
        "Checking for C++ compiler '-Wno-ambiguous-member-template' support",
        :cxx, '', '-Wno-ambiguous-member-template')
    end
    memoize :compiler_supports_wno_ambiguous_member_template?, true

    def self.cc_supports_feliminate_unused_debug?
      return cc_or_cxx_supports_feliminate_unused_debug?(:c)
    end
    memoize :cc_supports_feliminate_unused_debug?, true

    def self.cxx_supports_feliminate_unused_debug?
      return cc_or_cxx_supports_feliminate_unused_debug?(:cxx)
    end
    memoize :cxx_supports_feliminate_unused_debug?, true

    def self.cc_block_support_ok?
      return (os_name_simple != 'macosx' || cc_or_cxx_supports_blocks?(:c) || os_version >= "10.13" )
    end
    memoize :cc_block_support_ok?, true

    def self.cxx_block_support_ok?
      return (os_name_simple != 'macosx' || cc_or_cxx_supports_blocks?(:cxx) || os_version >= "10.13" )
    end
    memoize :cxx_block_support_ok?, true

    # Returns whether compiling C++ with -fvisibility=hidden might result
    # in tons of useless warnings, like this:
    # http://code.google.com/p/phusion-passenger/issues/detail?id=526
    # This appears to be a bug in older g++ versions:
    # http://gcc.gnu.org/ml/gcc-patches/2006-07/msg00861.html
    # Warnings should be suppressed with -Wno-attributes.
    def self.cc_visibility_flag_generates_warnings?
      if os_name_simple == "linux" && `#{cc} -v 2>&1` =~ /gcc version (.*?)/
        return $1 <= "4.1.2"
      else
        return false
      end
    end
    memoize :cc_visibility_flag_generates_warnings?, true

    def self.cxx_visibility_flag_generates_warnings?
      if os_name_simple == "linux" && `#{cxx} -v 2>&1` =~ /gcc version (.*?)/
        return $1 <= "4.1.2"
      else
        return false
      end
    end
    memoize :cxx_visibility_flag_generates_warnings?, true

    def self.address_sanitizer_flag
      if cc_is_clang?
        if `#{cc} --help` =~ /-fsanitize=/
          "-fsanitize=address"
        else
          "-faddress-sanitizer"
        end
      else
        nil
      end
    end
    memoize :address_sanitizer_flag

    def self.cxx_11_flag
      # C++11 support on FreeBSD 10.0 + Clang seems to be bugged.
      # http://llvm.org/bugs/show_bug.cgi?id=18310
      return nil if os_name_simple == "freebsd"

      source = %{
        struct Foo {
          Foo(Foo &&f) { }
        };
      }
      if try_compile("Checking for C++ -std=gnu++11 compiler flag", :cxx, source, '-std=gnu++11')
        return "-std=gnu++11"
      elsif try_compile("Checking for C++ -std=c++11 compiler flag", :cxx, source, '-std=c++11')
        return "-std=c++11"
      else
        return nil
      end
    end
    memoize :cxx_11_flag, true

    def self.has_rt_library?
      return try_link("Checking for -lrt support",
        :c, "int main() { return 0; }\n", '-lrt')
    end
    memoize :has_rt_library?, true

    def self.has_math_library?
      return try_link("Checking for -lmath support",
        :c, "int main() { return 0; }\n", '-lmath')
    end
    memoize :has_math_library?, true

    def self.has_dl_library?
      return try_link("Checking for -ldl support",
        :c, "int main() { return 0; }\n", '-ldl')
    end
    memoize :has_dl_library?, true

    def self.has_alloca_h?
      return try_compile("Checking for alloca.h",
        :c, '#include <alloca.h>')
    end
    memoize :has_alloca_h?, true

    def self.has_accept4?
      return try_compile("Checking for accept4()", :c, %Q{
        #define _GNU_SOURCE
        #include <sys/socket.h>
        static void *foo = accept4;
      })
    end
    memoize :has_accept4?, true

    # C compiler flags that should be passed in order to enable debugging information.
    def self.debugging_cflags
      # According to OpenBSD's pthreads man page, pthreads do not work
      # correctly when an app is compiled with -g. It recommends using
      # -ggdb instead.
      #
      # In any case we'll always want to use -ggdb for better GDB debugging.
      if cc_is_gcc?
        result = '-ggdb'
      else
        result = '-g'
      end
      if cc_supports_fno_limit_debug_info_flag?
        result << ' -fno-limit-debug-info'
      end
      result
    end

    # C++ compiler flags that should be passed in order to enable debugging information.
    def self.debugging_cxxflags
      # According to OpenBSD's pthreads man page, pthreads do not work
      # correctly when an app is compiled with -g. It recommends using
      # -ggdb instead.
      #
      # In any case we'll always want to use -ggdb for better GDB debugging.
      if cc_is_gcc?
        result = '-ggdb'
      else
        result = '-g'
      end
      if cxx_supports_fno_limit_debug_info_flag?
        result << ' -fno-limit-debug-info'
      end
      result
    end

    def self.export_dynamic_flags
      if os_name_simple == "linux"
        return '-rdynamic'
      else
        return nil
      end
    end


    def self.make
      return string_env('MAKE', find_command('make'))
    end
    memoize :make, true

    def self.gnu_make
      if result = string_env('GMAKE')
        return result
      else
        result = find_command('gmake')
        if !result
          result = find_command('make')
          if result
            if `#{result} --version 2>&1` =~ /GNU/
              return result
            else
              return nil
            end
          else
            return nil
          end
        else
          return result
        end
      end
    end
    memoize :gnu_make, true

    def self.xcode_select_version
      if find_command('xcode-select')
        `xcode-select --version` =~ /version (.+)\./
        return $1
      else
        return nil
      end
    end
  end

end # module PhusionPassenger

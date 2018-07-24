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
PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger

  module PlatformInfo
    # Extra flags that should always be passed to the C compiler
    # when linking, to be included last in the command string.
    def self.portability_c_ldflags
      return portability_c_or_cxx_ldflags(:c)
    end
    memoize :portability_c_ldflags

    # Extra flags that should always be passed to the C++ compiler
    # when linking, to be included last in the command string.
    def self.portability_cxx_ldflags
      return portability_c_or_cxx_ldflags(:cxx)
    end
    memoize :portability_cxx_ldflags

    # Extra compiler flags that should always be passed to the C compiler,
    # last in the command string.
    def self.default_extra_cflags
      return default_extra_c_or_cxxflags(:cc)
    end
    memoize :default_extra_cflags, true

    # Extra compiler flags that should always be passed to the C++ compiler,
    # last in the command string.
    def self.default_extra_cxxflags
      return default_extra_c_or_cxxflags(:cxx)
    end
    memoize :default_extra_cxxflags, true

  private
    def self.check_unordered_map(flags, class_name, header_name, macro_name)
      ok = try_compile("Checking for unordered_map", :cxx, %Q{
        #include <#{header_name}>
        int
        main() {
          #{class_name}<int, int> m;
          return 0;
        }
      })
      flags << "-D#{macro_name}" if ok
      return ok
    end
    private_class_method :check_unordered_map

    def self.check_hash_map(flags)
      hash_namespace = nil
      ok = false
      ['__gnu_cxx', '', 'std', 'stdext'].each do |namespace|
        ['hash_map', 'ext/hash_map'].each do |hash_map_header|
          ok = try_compile("Checking for #{hash_map_header}", :cxx, %Q{
            #include <#{hash_map_header}>
            int
            main() {
              #{namespace}::hash_map<int, int> m;
              return 0;
            }
          })
          if ok
            hash_namespace = namespace
            flags << "-DHASH_NAMESPACE=\"#{namespace}\""
            flags << "-DHASH_MAP_HEADER=\"<#{hash_map_header}>\""
            flags << "-DHASH_MAP_CLASS=\"hash_map\""
            break
          end
        end
        break if ok
      end
      ['ext/hash_fun.h', 'functional', 'tr1/functional',
       'ext/stl_hash_fun.h', 'hash_fun.h', 'stl_hash_fun.h',
       'stl/_hash_fun.h'].each do |hash_function_header|
        ok = try_compile("Checking for #{hash_function_header}", :cxx, %Q{
          #include <#{hash_function_header}>
          int
          main() {
            #{hash_namespace}::hash<int>()(5);
            return 0;
          }
        })
        if ok
          flags << "-DHASH_FUN_H=\"<#{hash_function_header}>\""
          break
        end
      end
    end
    private_class_method :check_hash_map

    def self.default_extra_c_or_cxxflags(cc_or_cxx)
      flags = ["-D_REENTRANT", "-I/usr/local/include"]

      if !send("#{cc_or_cxx}_is_sun_studio?")
        flags << "-Wall -Wextra -Wno-unused-parameter -Wno-parentheses -Wpointer-arith -Wwrite-strings -Wno-long-long"
        if send("#{cc_or_cxx}_supports_wno_missing_field_initializers_flag?")
          flags << "-Wno-missing-field-initializers"
        end
        if requires_no_tls_direct_seg_refs? && send("#{cc_or_cxx}_supports_no_tls_direct_seg_refs_option?")
          flags << "-mno-tls-direct-seg-refs"
        end
        # Work around Clang warnings in ev++.h.
        if send("#{cc_or_cxx}_is_clang?")
          flags << "-Wno-ambiguous-member-template"
        end
      end

      if !send("#{cc_or_cxx}_is_sun_studio?")
        if send("#{cc_or_cxx}_supports_feliminate_unused_debug?")
          flags << "-feliminate-unused-debug-symbols -feliminate-unused-debug-types"
        end
        if send("#{cc_or_cxx}_supports_visibility_flag?")
          flags << "-fvisibility=hidden -DVISIBILITY_ATTRIBUTE_SUPPORTED"
          if send("#{cc_or_cxx}_visibility_flag_generates_warnings?") &&
             send("#{cc_or_cxx}_supports_wno_attributes_flag?")
            flags << "-Wno-attributes"
          end
        end
      end

      flags << '-DHAS_ALLOCA_H' if has_alloca_h?
      flags << '-DHAVE_ACCEPT4' if has_accept4?
      flags << '-DHAS_SFENCE' if supports_sfence_instruction?
      flags << '-DHAS_LFENCE' if supports_lfence_instruction?
      flags << "-DPASSENGER_DEBUG -DBOOST_DISABLE_ASSERTS"

      if cc_or_cxx == :cxx
        flags << debugging_cxxflags
        flags << cxx_11_flag if cxx_11_flag

        if cxx_supports_wno_unused_local_typedefs_flag?
          # Avoids some compilaton warnings with Boost on Ubuntu 14.04.
          flags << "-Wno-unused-local-typedefs"
        end

        if cxx_supports_wno_format_nonliteral_flag?
          # SystemTools/UserDatabase.cpp uses snprintf() with a non-literal
          # format string.
          flags << '-Wno-format-nonliteral'
        end

        # There are too many implementations of of the hash map!
        # Figure out the right one.
        check_unordered_map(flags, "std::unordered_map", "unordered_map", "HAS_UNORDERED_MAP") ||
          check_unordered_map(flags, "std::tr1::unordered_map", "unordered_map", "HAS_TR1_UNORDERED_MAP") ||
          check_hash_map(flags)
      else
        flags << debugging_cflags
      end

      if os_name_simple == "solaris"
        if send("#{cc_or_cxx}_is_sun_studio?")
          flags << '-mt'
        else
          flags << '-pthreads'
        end
        if os_name_full =~ /solaris2\.11/
          # skip the _XOPEN_SOURCE and _XPG4_2 definitions in later versions of Solaris / OpenIndiana
          flags << '-D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
        else
          flags << '-D_XOPEN_SOURCE=500 -D_XPG4_2 -D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
          flags << '-D__SOLARIS9__ -DBOOST__STDC_CONSTANT_MACROS_DEFINED' if os_name_full =~ /solaris2\.9/
        end
        flags << '-DBOOST_HAS_STDINT_H' unless os_name_full =~ /solaris2\.9/
        if send("#{cc_or_cxx}_is_sun_studio?")
          flags << '-xtarget=ultra' if RUBY_PLATFORM =~ /sparc/
        else
          flags << '-mcpu=ultrasparc' if RUBY_PLATFORM =~ /sparc/
        end
      elsif os_name_simple == "openbsd"
        flags << '-DBOOST_HAS_STDINT_H -D_GLIBCPP__PTHREADS'
      elsif os_name_simple == "aix"
        flags << '-pthread'
        flags << '-DOXT_DISABLE_BACKTRACES'
      elsif RUBY_PLATFORM =~ /(sparc-linux|arm-linux|^arm.*-linux|sh4-linux)/
        # http://code.google.com/p/phusion-passenger/issues/detail?id=200
        # http://groups.google.com/group/phusion-passenger/t/6b904a962ee28e5c
        # http://groups.google.com/group/phusion-passenger/browse_thread/thread/aad4bd9d8d200561
        flags << '-DBOOST_SP_USE_PTHREADS'
      end

      return flags.compact.map{ |str| str.strip }.join(" ").strip
    end
    private_class_method :default_extra_c_or_cxxflags

    def self.portability_c_or_cxx_ldflags(cc_or_cxx)
      result = ''
      result << cxx_11_flag if cc_or_cxx == :cxx && cxx_11_flag
      if os_name_simple == "solaris"
        result << ' -lxnet -lsocket -lnsl -lpthread'
      else
        result << ' -lpthread'
      end
      result << ' -lrt' if has_rt_library?
      result << ' -lmath' if has_math_library?
      result << ' -ldl' if has_dl_library?
      result.strip!
      return result
    end
    private_class_method :portability_c_or_cxx_ldflags
  end

end # module PhusionPassenger

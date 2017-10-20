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

PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'platform_info/cxx_portability'

########## Phusion Passenger common library ##########

PhusionPassenger.require_passenger_lib 'common_library'


########## libboost_oxt ##########

# Defines tasks for compiling a static library containing Boost and OXT.
def define_libboost_oxt_task(namespace, output_dir, extra_compiler_flags = nil)
  output_file = "#{output_dir}.a"

  if OPTIMIZE
    optimize = "-O2"
    if LTO
      optimize << " -flto"
    end
  end

  # Define compilation targets for .cpp files in src/cxx_supportlib/vendor-modified/boost/src/pthread.
  boost_object_files = []
  Dir['src/cxx_supportlib/vendor-modified/boost/libs/**/*.cpp'].each do |source_file|
    object_name = File.basename(source_file.sub(/\.cpp$/, '.o'))
    boost_output_dir  = "#{output_dir}/boost"
    object_file = "#{boost_output_dir}/#{object_name}"
    boost_object_files << object_file

    define_cxx_object_compilation_task(
      object_file,
      source_file,
      lambda { {
        :include_paths => CXX_SUPPORTLIB_INCLUDE_PATHS,
        :flags => [optimize, maybe_eval_lambda(extra_compiler_flags)]
      } }
    )
  end

  # Define compilation targets for .cpp files in src/cxx_supportlib/oxt.
  oxt_object_files = []
  Dir['src/cxx_supportlib/oxt/*.cpp'].each do |source_file|
    object_name = File.basename(source_file.sub(/\.cpp$/, '.o'))
    oxt_output_dir  = "#{output_dir}/oxt"
    object_file = "#{oxt_output_dir}/#{object_name}"
    oxt_object_files << object_file

    define_cxx_object_compilation_task(
      object_file,
      source_file,
      lambda { {
        :include_paths => CXX_SUPPORTLIB_INCLUDE_PATHS,
        :flags => [optimize, maybe_eval_lambda(extra_compiler_flags)]
      } }
    )
  end

  object_files = boost_object_files + oxt_object_files

  file(output_file => object_files) do
    create_static_library(output_file, object_files)
  end

  task "#{namespace}:clean" do
    sh "rm -rf #{output_file} #{output_dir}"
  end

  if OPTIMIZE && LTO
    # Clang -flto does not support static libraries containing
    # .o files that are compiled with -flto themselves.
    [output_file, [output_file, boost_object_files, oxt_object_files].flatten.join(" ")]
  else
    [output_file, output_file]
  end
end


########## libev ##########

if USE_VENDORED_LIBEV
  LIBEV_SOURCE_DIR = File.expand_path("../src/cxx_supportlib/vendor-modified/libev", File.dirname(__FILE__)) + "/"
  LIBEV_TARGET = LIBEV_OUTPUT_DIR + ".libs/libev.a"

  let(:libev_cflags) do
    result = '-Isrc/cxx_supportlib/vendor-modified/libev'
    # Apple Clang 4.2 complains about ambiguous member templates in ev++.h.
    result << ' -Wno-ambiguous-member-template' if PlatformInfo.compiler_supports_wno_ambiguous_member_template?
    result
  end

  let(:libev_libs) do
    la_contents = File.open(LIBEV_OUTPUT_DIR + ".libs/libev.la", "r") do |f|
      f.read
    end
    la_contents =~ /dependency_libs='(.+)'/
    "#{LIBEV_OUTPUT_DIR}.libs/libev.a #{$1}".strip
  end

  task :libev => LIBEV_TARGET

  dependencies = [
    "src/cxx_supportlib/vendor-modified/libev/configure",
    "src/cxx_supportlib/vendor-modified/libev/config.h.in",
    "src/cxx_supportlib/vendor-modified/libev/Makefile.am"
  ]
  file LIBEV_OUTPUT_DIR + "Makefile" => dependencies do
    cc_command = cc
    cxx_command = cxx
    if OPTIMIZE && LTO
      cc_command = "#{cc_command} -flto"
      cxx_command = "#{cxx_command} -flto"
    end
    # Disable all warnings: http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#COMPILER_WARNINGS
    cflags = "#{extra_cflags} -w"
    sh "mkdir -p #{LIBEV_OUTPUT_DIR}" if !File.directory?(LIBEV_OUTPUT_DIR)
    sh "cd #{LIBEV_OUTPUT_DIR} && sh #{LIBEV_SOURCE_DIR}configure " +
      "--disable-shared --enable-static " +
      # libev's configure script may select a different default compiler than we
      # do, so we force our compiler choice.
      "CC='#{cc_command}' CXX='#{cxx_command}' CFLAGS='#{cflags}' orig_CFLAGS=1"
  end

  libev_sources = Dir["src/cxx_supportlib/vendor-modified/libev/{*.c,*.h}"]
  file LIBEV_OUTPUT_DIR + ".libs/libev.a" => [LIBEV_OUTPUT_DIR + "Makefile"] + libev_sources do
    sh "rm -f #{LIBEV_OUTPUT_DIR}libev.la"
    sh "cd #{LIBEV_OUTPUT_DIR} && make libev.la V=1"
  end

  task 'libev:clean' do
    patterns = %w(Makefile config.h config.log config.status libtool
      stamp-h1 *.o *.lo *.la .libs .deps)
    patterns.each do |pattern|
      sh "rm -rf #{LIBEV_OUTPUT_DIR}#{pattern}"
    end
  end

  task :clean => 'libev:clean'
else
  LIBEV_TARGET = nil

  let(:libev_cflags) do
    result = string_option('LIBEV_CFLAGS', '-I/usr/include/libev')
    # Apple Clang 4.2 complains about ambiguous member templates in ev++.h.
    result << ' -Wno-ambiguous-member-template' if PlatformInfo.compiler_supports_wno_ambiguous_member_template?
    result
  end

  let(:libev_libs) { string_option('LIBEV_LIBS', '-lev') }

  task :libev  # do nothing
end


########## libuv ##########

if USE_VENDORED_LIBUV
  LIBUV_SOURCE_DIR = File.expand_path("../src/cxx_supportlib/vendor-copy/libuv", File.dirname(__FILE__)) + "/"
  LIBUV_TARGET = LIBUV_OUTPUT_DIR + ".libs/libuv.a"

  let(:libuv_cflags) { '-Isrc/cxx_supportlib/vendor-copy/libuv/include' }

  let(:libuv_libs) do
    la_contents = File.open(LIBUV_OUTPUT_DIR + ".libs/libuv.la", "r") do |f|
      f.read
    end
    la_contents =~ /dependency_libs='(.+)'/
    "#{LIBUV_OUTPUT_DIR}.libs/libuv.a #{$1}".strip
  end

  task :libuv => LIBUV_TARGET

  dependencies = [
    "src/cxx_supportlib/vendor-copy/libuv/configure",
    "src/cxx_supportlib/vendor-copy/libuv/Makefile.am"
  ]
  file LIBUV_OUTPUT_DIR + "Makefile" => dependencies do
    cc_command = cc
    cxx_command = cxx
    if OPTIMIZE && LTO
      cc_command = "#{cc_command} -flto"
      cxx_command = "#{cxx_command} -flto"
    end
    # Disable all warnings. The author has a clear standpoint on that:
    # http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#COMPILER_WARNINGS
    cflags = "#{extra_cflags} -w"
    sh "mkdir -p #{LIBUV_OUTPUT_DIR}" if !File.directory?(LIBUV_OUTPUT_DIR)
    # Prevent 'make' from regenerating autotools files
    sh "cd #{LIBUV_SOURCE_DIR} && (touch aclocal.m4 configure Makefile.in || true)"
    sh "cd #{LIBUV_OUTPUT_DIR} && sh #{LIBUV_SOURCE_DIR}configure " +
      "--disable-shared --enable-static " +
      # libuv's configure script may select a different default compiler than we
      # do, so we force our compiler choice.
      "CC='#{cc_command}' CXX='#{cxx_command}' CFLAGS='#{cflags}'"
  end

  libuv_sources = Dir["src/cxx_supportlib/vendor-copy/libuv/**/{*.c,*.h}"]
  file LIBUV_OUTPUT_DIR + ".libs/libuv.a" => [LIBUV_OUTPUT_DIR + "Makefile"] + libuv_sources do
    sh "rm -f #{LIBUV_OUTPUT_DIR}/libuv.la"
    sh "cd #{LIBUV_OUTPUT_DIR} && make -j2 libuv.la V=1"
  end

  task 'libuv:clean' do
    patterns = %w(Makefile config.h config.log config.status libtool
      stamp-h1 src test *.o *.lo *.la *.pc .libs .deps)
    patterns.each do |pattern|
      sh "rm -rf #{LIBUV_OUTPUT_DIR}#{pattern}"
    end
  end

  task :clean => 'libuv:clean'
else
  LIBUV_TARGET = nil

  let(:libuv_cflags) { string_option('LIBUV_CFLAGS', '-I/usr/include/libuv') }
  let(:libuv_libs) { string_option('LIBUV_LIBS', '-luv') }

  task :libuv  # do nothing
end


########## WebSocket++ ##########

let(:websocketpp_cflags) { '-Isrc/cxx_supportlib/vendor-copy/websocketpp' }
let(:websocketpp_libs) { nil }


########## Shared definitions ##########
# Shared definition files should be in source control so that they don't
# have to be built by users. Users may not have write access to the source
# root, for example as is the case with Passenger Standalone.
#
# If you add a new shared definition file, don't forget to update
# src/ruby_supportlib/phusion_passenger/packaging.rb!

dependencies = ['src/cxx_supportlib/Constants.h.cxxcodebuilder',
  'src/ruby_supportlib/phusion_passenger.rb',
  'src/ruby_supportlib/phusion_passenger/constants.rb']
file 'src/cxx_supportlib/Constants.h' => dependencies do
  PhusionPassenger.require_passenger_lib 'constants'
  template = CxxCodeTemplateRenderer.new('src/cxx_supportlib/Constants.h.cxxcodebuilder')
  template.render_to('src/cxx_supportlib/Constants.h')
end


##############################


let(:libboost_oxt_cflags) do
  "" # Method reserved for future use
end

LIBBOOST_OXT, LIBBOOST_OXT_LINKARG = define_libboost_oxt_task(
  'common',
  "#{COMMON_OUTPUT_DIR}libboost_oxt",
  method(:libboost_oxt_cflags)
)
COMMON_LIBRARY.enable_optimizations!(LTO) if OPTIMIZE
COMMON_LIBRARY.define_tasks(method(:libboost_oxt_cflags))

# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2016 Phusion Holding B.V.
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

if USE_VENDORED_LIBCURL
  PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

  LIBCURL_SOURCE_DIR = SOURCE_ROOT / 'src/cxx_supportlib/vendor-copy/libcurl'
  LIBCURL_TARGET = LIBCURL_OUTPUT_DIR / 'lib/.libs/libcurl.a'
  LIBCURL_DEPENDENCY_TARGETS = [LIBCURL_TARGET, NGHTTP2_DEPENDENCY_TARGETS].flatten

  task :libcurl => LIBCURL_TARGET

  dependencies = [
    'src/cxx_supportlib/vendor-copy/libcurl/configure',
    'src/cxx_supportlib/vendor-copy/libcurl/Makefile.in',
    NGHTTP2_DEPENDENCY_TARGETS
  ].flatten
  file(LIBCURL_OUTPUT_DIR / 'Makefile' => dependencies) do
    cc = CC
    cxx = CXX
    if OPTIMIZE && LTO
      cc = "#{cc} -flto"
      cxx = "#{cxx} -flto"
    end
    e_nghttp_output_dir = shesc(NGHTTP2_OUTPUT_DIR.expand_path)

    sh "mkdir -p #{shesc LIBCURL_OUTPUT_DIR}" if !LIBCURL_OUTPUT_DIR.directory?

    # NOTE: if you disable any features, be sure to also update the disable
    # list in dev/vendor_libcurl
    sh "cd #{shesc LIBCURL_OUTPUT_DIR} && " \
      "env PKG_CONFIG_PATH=#{e_nghttp_output_dir}/lib:\"$PKG_CONFIG_PATH\" " \
      "sh #{shesc LIBCURL_SOURCE_DIR}/configure " \
      "--disable-silent-rules --disable-debug --enable-optimize --disable-werror " \
      "--disable-curldebug --enable-symbol-hiding " \
      "--disable-ares --disable-dependency-tracking --disable-shared --enable-static " \
      "--enable-http --disable-ftp --disable-file --disable-ldap --disable-ldaps " \
      "--disable-rtsp --enable-proxy --disable-dict --disable-telnet --disable-tftp " \
      "--disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher " \
      "--disable-manual --disable-libcurl-option --disable-versioned-symbols " \
      "--enable-threaded-resolver --disable-crypto-auth --enable-tls-srp --disable-unix-sockets " \
      "--enable-cookies --disable-soname-bump " \
      "--without-libpsl --without-libmetalink --without-libssh2 --without-librtmp " \
      "--without-libidn --without-zsh-functions-dir " \
      "--with-nghttp2=#{e_nghttp_output_dir} " \
      "#{libcurl_ssl_configure_options} " +
      # libcurl's configure script may select a different default compiler than we
      # do, so we force our compiler choice.
      "CC=#{shesc cc} " \
      "CXX=#{shesc cxx} " \
      "CFLAGS=#{shesc EXTRA_CFLAGS} " \
      "orig_CFLAGS=1"

    # Compile libcurl with -g. For example reason we can't pass that with CFLAGS.
    makefile = File.open("#{LIBCURL_OUTPUT_DIR}/lib/Makefile", 'rb') do |f|
      f.read
    end
    makefile.gsub!(/^CFLAGS = /, "CFLAGS = -g ")
    File.open("#{LIBCURL_OUTPUT_DIR}/lib/Makefile", 'wb') do |f|
      f.write(makefile)
    end
  end

  libcurl_sources = Dir[LIBCURL_SOURCE_DIR / 'lib/{*.c,*.h}']
  file(LIBCURL_TARGET => [LIBCURL_OUTPUT_DIR / 'Makefile'] + libcurl_sources) do
    sh "rm -f #{shesc LIBCURL_OUTPUT_DIR}/lib/libcurl.la"
    sh "cd #{shesc LIBCURL_OUTPUT_DIR}/lib && make -j2 libcurl.la"
  end

  task 'libcurl:clean' do
    sh "rm -rf #{shesc LIBCURL_OUTPUT_DIR}/*"
  end

  task :clean => 'libcurl:clean'

  def libcurl_ssl_configure_options
    if PlatformInfo.os_name_simple == 'macosx'
      '--with-darwinssl'
    end
  end

  def libcurl_cflags
    "-Isrc/cxx_supportlib/vendor-copy/libcurl/include " \
      "-I#{shesc LIBCURL_OUTPUT_DIR}/include/curl " \
      "-I#{shesc LIBCURL_OUTPUT_DIR}/lib " \
      "-DUSING_VENDORED_LIBCURL"
  end

  def libcurl_libs
    @libcurl_libs ||= begin
      la_contents = File.open(LIBCURL_OUTPUT_DIR / 'lib/.libs/libcurl.la', 'r') do |f|
        f.read
      end
      la_contents =~ /dependency_libs='(.+)'/
      result = "#{shesc LIBCURL_OUTPUT_DIR}/lib/.libs/libcurl.a #{$1}".strip
      if PlatformInfo.os_name_simple == 'macosx'
        result << " -framework Security -framework CoreFoundation"
      end
      result
    end
  end

else

  PhusionPassenger.require_passenger_lib 'platform_info/curl'

  LIBCURL_TARGET = nil
  LIBCURL_DEPENDENCY_TARGETS = []
  task :libcurl  # do nothing

  def libcurl_cflags
    @libcurl_cflags ||= compiler_flag_option('LIBCURL_CFLAGS', lambda { PlatformInfo.curl_flags })
  end

  def libcurl_libs
    @libcurl_libs ||= begin
      result = compiler_flag_option('LIBCURL_LIBS', lambda { PlatformInfo.curl_libs })
      result += " #{nghttp2_libs}"
      result.strip
    end
  end
end

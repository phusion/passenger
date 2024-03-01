define 'openssl-dev' do
  name "OpenSSL development headers"
  website "http://www.openssl.org/"
  define_checker do
    check_for_header('openssl/ssl.h', :c,
      PlatformInfo.openssl_extra_cflags)
  end

  on :debian do
    apt_get_install "libssl-dev"
  end
  on :redhat do
    yum_install "openssl-devel"
  end
  on :macosx do
    brew_install "openssl"
  end
end

define 'libcurl-dev' do
  name "Curl development headers with SSL support"
  website "http://curl.haxx.se/libcurl"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/curl'
    result = { :found => false }

    if !(curl_config = PlatformInfo.find_command('curl-config'))
      result[:error] = "Cannot find the `curl-config` command."
      next result
    else
      result["curl-config location"] = curl_config
    end

    if !(header = PlatformInfo.find_header("curl/curl.h", :c, PlatformInfo.curl_flags))
      result[:error] = "Cannot find the curl/curl.h header file."
      next result
    else
      result[:found] = true
      result["Header location"] = header == true ? "somewhere, not sure where" : header
    end

    begin
      result["Version"] = `#{curl_config} --version`.strip
    rescue SystemCallError => e
      result[:error] = "Cannot run `curl-config --version`: #{e}"
      next result
    end

    source = %Q{
      #include <curl/curl.h>
      int main() {
        CURLcode result = curl_global_init(CURL_GLOBAL_ALL);
        if (result == CURLE_PEER_FAILED_VERIFICATION) { // fails to compile if too old
          return 1;
        }
        return 0;
      }
    }
    ret = PlatformInfo.try_compile_and_run("Checking for libcurl usability", :c, source,
      "#{PlatformInfo.curl_flags} #{PlatformInfo.curl_libs}")
    result["Usable"] = ret ? "yes" : "no"
    if !ret
      result[:error] = "libcurl was found, but it isn't usable. Set VERBOSE=1 to see why."
      next result
    end

    result["Supports SSL"] = PlatformInfo.curl_supports_ssl? ? "yes" : "no"
    if !PlatformInfo.curl_supports_ssl?
      result[:error] = "libcurl was found, but it doesn't support SSL. Please reinstall it with SSL support."
      next result
    end

    result
  end

  install_instructions "Please download Curl from <b>#{website}</b> " +
    "and make sure you install it <b>with SSL support</b>."
  on :debian do
    install_instructions "Please run " +
      "<b>apt-get install libcurl4-openssl-dev</b> " +
      "or <b>libcurl4-gnutls-dev</b>, whichever you prefer."
  end
  on :redhat do
    release = PlatformInfo.read_file("/etc/redhat-release")
    if release =~ /release 4/
      # http://code.google.com/p/phusion-passenger/issues/detail?id=554
      yum_install "curl-devel zlib-devel e2fsprogs-devel krb5-devel libidn-devel"
    elsif release =~ /release 5/
      yum_install "curl-devel"
    else
      yum_install "libcurl-devel"
    end
  end
end

define 'zlib-dev' do
  name "Zlib development headers"
  website "http://www.zlib.net/"
  define_checker do
    check_for_header('zlib.h')
  end

  on :debian do
    apt_get_install "zlib1g-dev"
  end
  on :mandriva do
    urpmi "zlib1-devel"
  end
  on :redhat do
    yum_install "zlib-devel"
  end
end

define 'pcre2-dev' do
  name "PCRE2 development headers"
  website "http://www.pcre.org/"
  define_checker do
    check_for_header('pcre2.h', :c,
      PlatformInfo.pcre_extra_cflags)
  end

  on :debian do
    apt_get_install "libpcre2-dev"
  end
  on :redhat do
    yum_install 'pcre2-devel'
  end
  on :macosx do
    brew_install "pcre2"
  end
end

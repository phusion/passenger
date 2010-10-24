# RPM Spec file for Phusion Passenger
#

%define gemname passenger
%define passenger_version 3.0.0
%define passenger_release 4%{?dist}
%define passenger_epoch 1

%define nginx_version 0.8.52
%define nginx_release %{passenger_version}_%{passenger_release}
%define nginx_user	passenger
%define nginx_group	%{nginx_user}
%define nginx_home      %{_localstatedir}/lib/nginx
%define nginx_home_tmp  %{nginx_home}/tmp
%define nginx_logdir    %{_localstatedir}/log/nginx
%define nginx_confdir   %{_sysconfdir}/nginx
%define nginx_datadir   %{_datadir}/nginx
%define nginx_webroot   %{nginx_datadir}/html

%define httpd_confdir	%{_sysconfdir}/httpd/conf.d

# Macros on the command-line overrides these defaults. You should also
# make sure these match the binaries found in your PATH
%{?!ruby: %define ruby /usr/bin/ruby}
%{?!rake: %define rake /usr/bin/rake}
%{?!gem:  %define gem  /usr/bin/gem}

%define ruby_sitelib %(%{ruby} -rrbconfig -e "puts Config::CONFIG['sitelibdir']")

%define ruby_version_patch %(%{ruby} -e 'puts "#{RUBY_VERSION}.#{RUBY_PATCHLEVEL}"')

# Does Gem::Version crash&burn on the version defined above? (RHEL might)
%define broken_gem_version %(%{ruby} -rrubygems -e 'begin ; Gem::Version.create "%{passenger_version}" ; rescue => e ; puts 1 ; exit ; end ; puts 0')

%if %{broken_gem_version}
  # Strip any non-numeric version part
  %define gemversion %(echo '%{passenger_version}'|sed -e 's/\\.[^.]*[^.0-9]\\+[^.]*//g')
%else
  %define gemversion %{passenger_version}
%endif

# Invoke a shell to do a comparison, silly but it works across versions of RPM
%define gem_version_mismatch %([ '%{passenger_version}' != '%{gemversion}' ] && echo 1 || echo 0)

%define gemdir %(%{ruby} -rubygems -e 'puts Gem::dir' 2>/dev/null)
%define geminstdir %{gemdir}/gems/%{gemname}-%{gemversion}

# This may cause a chicken/egg problem where the dir isn't defined yet
%define gemnativedir %(%{ruby} -I%{_builddir}/%{gemname}-%{passenger_version}/lib -rphusion_passenger/platform_info/binary_compatibility -e 'puts PhusionPassenger::PlatformInfo.ruby_extension_binary_compatibility_ids.join("-")')
%define native_libs_release %(%{ruby} -I%{_builddir}/%{gemname}-%{passenger_version}/lib -rphusion_passenger/platform_info/binary_compatibility -e 'puts PhusionPassenger::PlatformInfo.ruby_extension_binary_compatibility_ids[0,2].join("_")')

%{!?only_native_libs: %define only_native_libs 0}

Summary: Easy and robust Ruby web application deployment
Name: rubygem-%{gemname}
Version: %{passenger_version}
Release: %{passenger_release}
Group: System Environment/Daemons
License: Modified BSD
URL: http://www.modrails.com/
Source0: %{gemname}-%{passenger_version}.tar.gz
Source1: nginx-%{nginx_version}.tar.gz
Source100: apache-passenger.conf.in
Source101: nginx-passenger.conf.in
Patch0: passenger-install-nginx-module.patch
BuildRoot: %{_tmppath}/%{name}-%{passenger_version}-%{passenger_release}-root-%(%{__id_u} -n)
Requires: rubygems
Requires: rubygem(rake) >= 0.8.1
Requires: rubygem(fastthread) >= 1.0.1
Requires: rubygem(daemon_controller) >= 0.2.5
Requires: rubygem(file-tail)
Requires: rubygem(rack)
BuildRequires: ruby-devel
BuildRequires: httpd-devel
BuildRequires: rubygems
BuildRequires: rubygem(rake) >= 0.8.1
BuildRequires: rubygem(fastthread) >= 1.0.1
BuildRequires: doxygen
BuildRequires: asciidoc
# Can't have a noarch package with an arch'd subpackage
#BuildArch: noarch
Provides: rubygem(%{gemname}) = %{passenger_version}
Epoch: %{passenger_epoch}

%description
Phusion Passenger™ — a.k.a. mod_rails or mod_rack — makes deployment
of Ruby web applications, such as those built on the revolutionary
Ruby on Rails web framework, a breeze. It follows the usual Ruby on
Rails conventions, such as “Don’t-Repeat-Yourself”.

%if %{gem_version_mismatch}
**NOTE: Because the default Gem::Version doesn't accept the correct
version, it is installed as %{gemversion} instead of %{passenger_version}.
%endif

%if !%{only_native_libs}

%package native
Summary: Phusion Passenger native extensions
Group: System Environment/Daemons
Requires: %{name} = %{passenger_epoch}:%{passenger_version}-%{passenger_release}
Epoch: %{passenger_epoch}
%description native
Phusion Passenger™ — a.k.a. mod_rails or mod_rack — makes deployment
of Ruby web applications, such as those built on the revolutionary
Ruby on Rails web framework, a breeze. It follows the usual Ruby on
Rails conventions, such as “Don’t-Repeat-Yourself”.

This package contains the native code extensions for Apache & Nginx bindings

%endif #! only_native_libs

%package native-libs
Summary: Phusion Passenger native extensions
Group: System Environment/Daemons
Release: %{native_libs_release}
Epoch: %{passenger_epoch}
Requires: %{name}-native = %{passenger_epoch}:%{passenger_version}-%{passenger_release}
Requires: ruby = %{ruby_version_patch}
Provides: rubygem-passenger-native-libs = %{passenger_epoch}:%{passenger_version}-%{passenger_release}
%description native-libs
Phusion Passenger™ — a.k.a. mod_rails or mod_rack — makes deployment
of Ruby web applications, such as those built on the revolutionary
Ruby on Rails web framework, a breeze. It follows the usual Ruby on
Rails conventions, such as “Don’t-Repeat-Yourself”.

This package contains the native shared library for Apache & Nginx
bindings, built against ruby sources. It has been separated so that
installing a new ruby interpreter only necessitates rebuilding this
package.

%if !%{only_native_libs}

%package standalone
Summary: Standalone Phusion Passenger Server
Group: System Environment/Daemons
Requires: %{name} = %{passenger_epoch}:%{passenger_version}-%{passenger_release}
Epoch: %{passenger_epoch}
%description standalone
Phusion Passenger™ — a.k.a. mod_rails or mod_rack — makes deployment
of Ruby web applications, such as those built on the revolutionary
Ruby on Rails web framework, a breeze. It follows the usual Ruby on
Rails conventions, such as “Don’t-Repeat-Yourself”.

This package contains the standalone Passenger server

%package -n mod_passenger
Summary: Apache Module for Phusion Passenger
Group: System Environment/Daemons
Requires: %{name}-native = %{passenger_epoch}:%{passenger_version}-%{passenger_release}
#BuildArch: %_target_arch
Obsoletes: rubygem-passenger-apache
Epoch: %{passenger_epoch}
%description -n mod_passenger
Phusion Passenger™ — a.k.a. mod_rails or mod_rack — makes deployment
of Ruby web applications, such as those built on the revolutionary
Ruby on Rails web framework, a breeze. It follows the usual Ruby on
Rails conventions, such as “Don’t-Repeat-Yourself”.

This package contains the pluggable Apache server module for Passenger.

%package -n nginx-passenger
Summary: nginx server with Phusion Passenger enabled
Group: System Environment/Daemons
Requires: %{name} = %{passenger_epoch}:%{passenger_version}
Version: %{nginx_version}
Release: %{passenger_version}_%{passenger_release}
BuildRequires: pcre-devel
BuildRequires: zlib-devel
BuildRequires: openssl-devel
Requires: %{name}-native-libs = %{passenger_epoch}:%{passenger_version}-%{passenger_release}
Requires: pcre
Requires: zlib
Requires: openssl
Requires: nginx-alternatives
Epoch: %{passenger_epoch}
%description -n nginx-passenger
Phusion Passenger™ — a.k.a. mod_rails or mod_rack — makes deployment
of Ruby web applications, such as those built on the revolutionary
Ruby on Rails web framework, a breeze. It follows the usual Ruby on
Rails conventions, such as “Don’t-Repeat-Yourself”.

This package includes an nginx server with Passenger compiled in.

%endif # !only_native_libs

%prep
%setup -q -n %{gemname}-%{passenger_version} -b 1
%patch0 -p1

%if %{gem_version_mismatch}
  %{warn:
***
*** WARNING: Your Gem::Version crashes on '%{passenger_version},'
***          Falling back to use '%gemversion' internally
***

}
  # Use sed rather than a patch, so it's more resilliant to version changes
  sed -i -e "s/\(^[ \t]VERSION_STRING *= *'[0-9]\+\.[0-9]\+\.[0-9]\+\)[^']\+/\1/" lib/phusion_passenger.rb
  sed -i -e 's/^\(#define PASSENGER_VERSION "[0-9]\+\.[0-9]\+\.[0-9]\+\)[^"]\+/\1/' ext/common/Constants.h
%endif

%build

%if %{only_native_libs}
  %{rake} native_support
%else
  %{rake} package
  ./bin/passenger-install-apache2-module --auto
%endif # !only_native_libs

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{gemdir}

%if !%{only_native_libs}
%{gem} install --local --install-dir %{buildroot}%{gemdir} \
               --force --rdoc pkg/%{gemname}-%{gemversion}.gem
mkdir -p %{buildroot}/%{_bindir}
mv %{buildroot}%{gemdir}/bin/* %{buildroot}/%{_bindir}
rmdir %{buildroot}%{gemdir}/bin
# Nothing there
# find %{buildroot}%{geminstdir}/bin -type f | xargs chmod a+x

# RPM finds these in shebangs and assumes they're requirements. Clean them up.
find %{buildroot}%{geminstdir} -type f -print0 | xargs -0 perl -pi -e 's{#!(/opt/ruby.*|/usr/bin/ruby1.8)}{%{ruby}}g'

mkdir -p %{buildroot}/%{_libdir}/httpd/modules
install -m 0644 ext/apache2/mod_passenger.so %{buildroot}/%{_libdir}/httpd/modules

mkdir -p %{buildroot}/%{nginx_datadir}
mkdir -p %{buildroot}/%{nginx_datadir}
mkdir -p %{buildroot}/%{nginx_confdir}
mkdir -p %{buildroot}/%{nginx_logdir}
mkdir -p %{buildroot}/%{httpd_confdir}

##### Nginx. This should probably be in the %%build, with apache, but
##### it installs directly

# THIS is beyond ugly. But it corrects the check-buildroot error on
# the string saved for 'nginx -V'
#
# In any case, fix it correctly later
perl -pi -e 's{^install:\s*$}{$&\tperl -pi -e '\''s<%{buildroot}><>g;s<%{_builddir}><%%{_builddir}>g;'\'' objs/ngx_auto_config.h\n}' %{_builddir}/nginx-%{nginx_version}/auto/install

### Stolen [and hacked] from the nginx spec file
export DESTDIR=%{buildroot}
./bin/passenger-install-nginx-module --auto --nginx-source-dir=%{_builddir}/nginx-%{nginx_version} --prefix=%{buildroot}/%{nginx_datadir} --extra-make-install-flags='DESTDIR=%{buildroot} INSTALLDIRS=vendor' --extra-configure-flags="--user=%{nginx_user} \
    --group=%{nginx_group} \
    --prefix=%{nginx_datadir} \
    --sbin-path=%{_sbindir}/nginx.passenger \
    --conf-path=%{nginx_confdir}/nginx.conf \
    --error-log-path=%{nginx_logdir}/error.log \
    --http-log-path=%{nginx_logdir}/access.log \
    --http-client-body-temp-path=%{nginx_home_tmp}/client_body \
    --http-proxy-temp-path=%{nginx_home_tmp}/proxy \
    --http-fastcgi-temp-path=%{nginx_home_tmp}/fastcgi \
    --pid-path=%{_localstatedir}/run/nginx.pid \
    --lock-path=%{_localstatedir}/lock/subsys/nginx \
    --with-http_ssl_module \
    --with-http_realip_module \
    --with-http_addition_module \
    --with-http_sub_module \
    --with-http_dav_module \
    --with-http_flv_module \
    --with-http_gzip_static_module \
    --with-http_stub_status_module \
"
#     --with-cc-opt='%{optflags} %(pcre-config --cflags)' \
#     --add-module=%{_builddir}/%{gemname}-%{passenger_version}/nginx-%{nginx_version}/nginx-upstream-fair \

# I should probably figure out how to get these into the gem
cp -ra agents %{buildroot}/%{geminstdir}
%endif #!only_native_libs

##### NATIVE LIBS INSTALL
mkdir -p %{buildroot}/%{geminstdir}/ext/ruby
cp -ra ext/ruby/*-linux %{buildroot}/%{geminstdir}/ext/ruby

%if !%{only_native_libs}
# Clean up everything we don't care about
rm -rf %{buildroot}/usr/share/nginx %{buildroot}/%{nginx_confdir}
install -p -d -m 0755 %{buildroot}/%{nginx_confdir}/conf.d
#install -m 0644 %{SOURCE100} %{buildroot}/%{httpd_confdir}/passenger.conf
#install -m 0644 %{SOURCE101} %{buildroot}/%{nginx_confdir}/conf.d/passenger.conf
perl -pe 's{%%ROOT}{%geminstdir}g;s{%%RUBY}{%ruby}g' %{SOURCE100} > %{buildroot}/%{httpd_confdir}/passenger.conf
perl -pe 's{%%ROOT}{%geminstdir}g;s{%%RUBY}{%ruby}g' %{SOURCE101} > %{buildroot}/%{nginx_confdir}/conf.d/passenger.conf

%post -n nginx-passenger
if [ $1 == 1 ]; then
  /usr/sbin/alternatives --install /usr/sbin/nginx nginx /usr/sbin/nginx.passenger 50
fi

%postun -n nginx-passenger
if [ $1 == 0 ]; then
  /usr/sbin/alternatives --remove nginx /usr/sbin/nginx.passenger
fi
%endif # !only_native_libs

%clean
rm -rf %{buildroot}

%if !%{only_native_libs}
%files
%defattr(-, root, root, -)
%{_bindir}/passenger-install-apache2-module
%{_bindir}/passenger-install-nginx-module
%{_bindir}/passenger-config
%{_bindir}/passenger-stress-test
%{_bindir}/passenger-status
%{_bindir}/passenger-memory-stats
%{_bindir}/passenger-make-enterprisey
%{gemdir}/gems/%{gemname}-%{gemversion}/
%doc %{gemdir}/doc/%{gemname}-%{gemversion}
%doc %{geminstdir}/README
%{gemdir}/cache/%{gemname}-%{gemversion}.gem
%{gemdir}/specifications/%{gemname}-%{gemversion}.gemspec

%files native
%{geminstdir}/agents

%files standalone
%doc doc/Users\ guide\ Standalone.html
%doc doc/Users\ guide\ Standalone.txt
%{_bindir}/passenger

%files -n mod_passenger
%doc doc/Users\ guide\ Apache.html
%doc doc/Users\ guide\ Apache.txt
%{_libdir}/httpd/modules/mod_passenger.so
%{httpd_confdir}/passenger.conf

%files -n nginx-passenger
%doc doc/Users\ guide\ Nginx.html
%doc doc/Users\ guide\ Nginx.txt
%{nginx_confdir}/conf.d/passenger.conf
/usr/sbin/nginx.passenger
%endif # !only_native_libs

%files native-libs
%{geminstdir}/ext/ruby/%{gemnativedir}

%changelog
* Sat Oct 23 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0-4
- --define 'only_native_libs 1' to rebuild native_support.so for a
  different ruby engine.

* Fri Oct 22 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0-3
- Break the passenger_native_support.so into its own package

* Thu Oct 21 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0-2
- rename rubygem-passenger-apache => mod_passenger

* Thu Oct 21 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0-1
- Version bump to 3.0.0

* Wed Oct 18 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0.pre4-2
- use nginx-alternatives

* Sun Oct 17 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0.pre4-1
- Nginx suport

* Mon Oct 11 2010 Erik Ogan <erik@stealthymonkeys.com> - 3.0.0.pre4-0
- Test for Gem::Version issues with the version and work around it.
- Initial Spec File

# RPM Spec file for nginx-alternatives
# This is a stop-gap solution to having multiple compiles of nginx on
# the same server.
#
# This package is meant to be obsoleted by a future nginx package that
# will provide the same feature

%define perldir %(perl -MConfig -e 'print $Config{installarchlib}')

Summary: Alternatives aware nginx
Name: nginx-alternatives
Version: 0.0.1
Release: 3%{?dist}
License: MIT
Group: System Environment/Daemons
#Source0: %{name}-%{version}.tar.gz
Source0: README.%{name}
Requires: nginx
BuildArch: noarch
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
This package puts the nginx webserver binary under the control of the
/etc/alternative system.

This package is meant to be obsoleted by a future nginx package (which
will provide the same feature)

%prep
#%setup -q

%build
cp %{SOURCE0} .

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%triggerin -- nginx
if [ ! -L /usr/sbin/nginx ] ; then
  mv /usr/sbin/nginx /usr/sbin/nginx.base
  mv %{perldir}/auto/nginx/nginx.so  %{perldir}/auto/nginx/nginx_base.so
  mv %{perldir}/nginx.pm  %{perldir}/nginx_base.pm
  mv %{_mandir}/man3/nginx.3pm.gz %{_mandir}/man3/nginx_base.3pm.gz

  /usr/sbin/alternatives --install /usr/sbin/nginx nginx \
				   /usr/sbin/nginx.base 30 \
    --slave %{perldir}/auto/nginx/nginx.so nginx.so \
	    %{perldir}/auto/nginx/nginx_base.so \
    --slave %{perldir}/nginx.pm nginx.pm %{perldir}/nginx_base.pm \
    --slave %{_mandir}/man3/nginx.3pm.gz nginx.man \
	    %{_mandir}/man3/nginx_base.3pm.gz
fi

# Given that other packages will depend on this one, it's 99% likely
# that this will have been reset back to base. Still good practice to
# put the expected binary back in place.
%define undo_link \
  bin=`readlink -f /usr/sbin/nginx` \
  so=`readlink -f %{perldir}/auto/nginx/nginx.so` \
  pm=`readlink -f %{perldir}/nginx.pm` \
  man=`readlink -f %{_mandir}/man3/nginx.3pm.gz` \
  /usr/sbin/alternatives --remove nginx /usr/sbin/nginx.base \
  /usr/sbin/alternatives --remove nginx $bin \
  mv -f $bin /usr/sbin/nginx \
  mv -f $so %{perldir}/auto/nginx/nginx.so \
  mv -f $pm %{perldir}/nginx.pm \
  mv -f $man %{_mandir}/man3/nginx.3pm.gz


%triggerun -- nginx
if [ -L /usr/sbin/nginx ] ; then
  %undo_link
fi

# triggerun runs if either package is removed, so this isn't necessary
%postun
if [ $1 == 0 -a -L /usr/sbin/nginx ]; then
  %undo_link
fi

%files
%defattr(-,root,root,-)
%doc README.%{name}

%changelog
* Sat Oct 30 2010 Erik Ogan <erik@stealthymonkeys.com> - 0.0.1-3
- Add slaves for the perl module

* Thu Oct 21 2010 Erik Ogan <erik@stealthymonkeys.com> - 0.0.1-2
- Use triggers to maintain the link if nginx package is upgraded

* Wed Oct 20 2010 Erik Ogan <erik@stealthymonkeys.com> - 0.0.1-1
- Initial build.

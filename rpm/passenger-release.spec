Summary: Phusion Passenger release RPM/Yum repository configuration
Name: passenger-release
Version: 3
Release: 3%{?dist}
License: MIT
Group: Group: System Environment/Base
URL: http://passenger.stealthymonkeys.com/
Source0: mirrors-passenger
Source1: RPM-GPG-KEY-stealthymonkeys
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildArch: noarch

%description
Phusion Passenger Yum/RPM configuration. This package contains the Yum
repository configuration to install & update Phusion Passenger, as
well as the GPG signing key to verify them.

%prep
#%setup -c

%{?el5:name='Red Hat Enterprise' version='5' path='rhel'}
%{?el6:name='Red Hat Enterprise' version='5' path='rhel'}

%{?fc13:name='Fedora Core' version='13' path='fedora'}
%{?fc14:name='Fedora Core' version='13' path='fedora'}

if [ -z "$name" ] ; then
 echo "Please specify a distro to build for (f'rex: el5 or fc13)" >&2
 exit 255
fi

%{__cat} <<EOF > passenger.repo
### Name: Phusion Passenger RPM Repository for $name $version
### URL: %{url}
[passenger]
name = $name \$releasever - Phusion Passenger
baseurl = %{url}$path/\$releasever/\$basearch
mirrorlist = %{url}$path/mirrors
#mirrorlist = file:///etc/yum.repos.d/mirrors-passenger
enabled = 1
gpgkey = file:///etc/pki/rpm-gpg/RPM-GPG-KEY-passenger
gpgcheck = 1

### Name: Phusion Passenger RPM Repository for $name $version (TESTING)
### URL: %{url}
[passenger-testing]
name = $name \$releasever - Phusion Passenger - TEST
baseurl = %{url}$path/\$releasever/\$basearch/testing/
enabled = 0
gpgkey = file:///etc/pki/rpm-gpg/RPM-GPG-KEY-passenger
gpgcheck = 0
EOF

for mirror in $(%{__cat} %{SOURCE0}); do
  echo "$mirror/$path/\$releasever/en/\$ARCH/"
done > mirrors-passenger

%build

%install
rm -rf %{buildroot}
%{__install} -D -p -m 0644 %{SOURCE1} %{buildroot}%{_sysconfdir}/pki/rpm-gpg/RPM-GPG-KEY-passenger
%{__install} -D -p -m 0644 passenger.repo %{buildroot}%{_sysconfdir}/yum.repos.d/passenger.repo



%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
%doc mirrors-passenger
%{_sysconfdir}/pki/rpm-gpg/RPM-GPG-KEY-passenger
%{_sysconfdir}/yum.repos.d/passenger.repo



%changelog
* Thu Oct 28 2010 Erik Ogan <erik@stealthymonkeys.com> - 3-3
- Typo in the gpgkey directives

* Thu Oct 28 2010 Erik Ogan <erik@stealthymonkeys.com> - 3-2
- Update the mirrorlist URL

* Tue Oct 26 2010 Erik Ogan <erik@stealthymonkeys.com> - 3-1
- Initial build.


Name:          libks
Version:       %{version}
Release:       1
Summary:       SignalWire's libks
License:       MIT
Group:         Development/Libraries/C and C++
Url:           https://signalwire.com/
Source:        libks-%{version}.tar.gz

BuildRequires: git
BuildRequires: gcc-c++
BuildRequires: cmake3
BuildRequires: libuuid-devel
BuildRequires: pkgconfig

BuildRequires: libatomic
BuildRequires: openssl-devel

%if 0%{?is_opensuse}
BuildRequires: libopenssl-devel
%endif

%if 0%{?fedora}
BuildRequires: libatomic
BuildRequires: openssl-devel
%endif

BuildRoot:     %{_tmppath}/%{name}-%{version}-build

%description
SignalWire's LibKitchenSink

%package devel
Summary:       SignalWire's libks
Group:         Development/Libraries/C and C++
Requires:	   %{name} = %{version}

%description devel
SignalWire's LibKitchenSink. Development files

%prep
%setup -n libks-1.6.0

%build
cmake3 . -DCMAKE_BUILD_TYPE=Release
make %{?_smp_mflags}

%install
%make_install

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root)
/usr/lib/libks.so*
/usr/share/doc/libks

%files devel
%defattr(-,root,root)
%{_includedir}/libks/
/usr/lib/pkgconfig/libks.pc

%changelog
* Tue Jan 29 2019 - alex@freeswitch.com 
- Initial build
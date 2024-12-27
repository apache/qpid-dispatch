#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Copyright Fedora Project Authors.
# This code is licensed under MIT license

# Currently orphaned in Fedora
# - https://pagure.io/releng/issue/12278
# - https://bugzilla.redhat.com/2245601

# This package depends on automagic byte compilation
# https://fedoraproject.org/wiki/Changes/No_more_automagic_Python_bytecompilation_phase_2
%global py_byte_compile 1
%global __cmake_in_source_build 1

# Define pkgdocdir for releases that don't define it already
%{!?_pkgdocdir: %global _pkgdocdir %{_docdir}/%{name}-%{version}}

%global _console_subpackage 0
%global _use_systemd 1

%global proton_minimum_version 0.34.0
%global libwebsockets_minimum_version 3.2.0
%global libnghttp2_minimum_version 1.33.0

%undefine __brp_mangle_shebangs

Name:          qpid-dispatch
Version:       3.1.0
Release:       9.20250113093119135559.jd_fix_build.65.g76e0c22d%{?dist}
Summary:       Dispatch router for Qpid
License:       ASL 2.0
URL:           http://qpid.apache.org/
Source0:       qpid-dispatch-3.1.0.tar.gz

Source1:       licenses.xml
%if %{_console_subpackage}
Source2:       qpid-dispatch-console-%{version}.tar.gz
%endif

%global _pkglicensedir %{_licensedir}/%{name}-%{version}
%{!?_licensedir:%global license %doc}
%{!?_licensedir:%global _pkglicensedir %{_pkgdocdir}}

Patch1:        dispatch.patch
Patch2:        qpid-dispatch-1.19.0.patch

BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: cmake
BuildRequires: qpid-proton-c-devel >= %{proton_minimum_version}
BuildRequires: python3-devel
BuildRequires: python3-qpid-proton >= %{proton_minimum_version}
BuildRequires: openssl-devel
BuildRequires: libwebsockets-devel >= %{libwebsockets_minimum_version}
BuildRequires: libnghttp2-devel >= %{libnghttp2_minimum_version}
BuildRequires: asciidoc >= 8.6.8
BuildRequires: systemd
BuildRequires: python3-setuptools
%if 0%{?fedora}
BuildRequires: rubygem-asciidoctor
%endif

%description
A lightweight message router, written in C and built on Qpid Proton, that provides flexible and scalable interconnect between AMQP endpoints or between endpoints and brokers.


%package router
Summary:  The Qpid Dispatch Router executable
Obsoletes: libqpid-dispatch
Obsoletes: libqpid-dispatch-devel
Requires:  qpid-proton-c%{?_isa} >= %{proton_minimum_version}
Requires:  python3
Requires:  python3-qpid-proton >= %{proton_minimum_version}
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd
Requires: libwebsockets >= %{libwebsockets_minimum_version}
Requires: libnghttp2 >= %{libnghttp2_minimum_version}

%description router
%{summary}.

%files router
%license %{_pkglicensedir}/LICENSE
%license %{_pkglicensedir}/licenses.xml
%{_sbindir}/qdrouterd
%config(noreplace) %{_sysconfdir}/qpid-dispatch/qdrouterd.conf
%config(noreplace) %{_sysconfdir}/sasl2/qdrouterd.conf
%{_exec_prefix}/lib/qpid-dispatch

%{python3_sitelib}/qpid_dispatch_site.py*
%{python3_sitelib}/qpid_dispatch
%{python3_sitelib}/qpid_dispatch-*.egg-info
%{python3_sitelib}/__pycache__/*

%{_unitdir}/qdrouterd.service
%{_mandir}/man5/qdrouterd.conf.5*
%{_mandir}/man8/qdrouterd.8*

%pre router
getent group qdrouterd >/dev/null || groupadd -r qdrouterd
getent passwd qdrouterd >/dev/null || \
  useradd -r -M -g qdrouterd -d %{_localstatedir}/lib/qdrouterd -s /sbin/nologin \
    -c "Owner of Qdrouterd Daemons" qdrouterd
exit 0


%post router
/sbin/ldconfig
%systemd_post qdrouterd.service

%preun router
%systemd_preun qdrouterd.service

%postun router
/sbin/ldconfig
%systemd_postun_with_restart qdrouterd.service


%package docs
Summary:   Documentation for the Qpid Dispatch router
BuildArch: noarch
Obsoletes:  qpid-dispatch-router-docs

%description docs
%{summary}.

%files docs
%doc %{_pkgdocdir}
%license %{_pkglicensedir}/LICENSE
%license %{_pkglicensedir}/licenses.xml


%if %{_console_subpackage}
%package console
Summary: Web console for Qpid Dispatch Router
BuildArch: noarch
Requires: qpid-dispatch-router
%description console
%{summary}.

%files console
%{_datarootdir}/qpid-dispatch/console
%endif

%package tools
Summary: Tools for the Qpid Dispatch router
BuildArch: noarch
Requires: python3-qpid-proton >= %{proton_minimum_version}

%description tools
%{summary}.

%files tools
%{_bindir}/qdstat
%{_bindir}/qdmanage

%{_mandir}/man8/qdstat.8*
%{_mandir}/man8/qdmanage.8*


%prep
%setup -q -n qpid-dispatch-3.1.0
%patch1 -p1
%patch2 -p1
%patch3 -p1

mkdir pre_built
cd pre_built
#tar xvzpf %{SOURCE2} -C .

%build
%cmake -DDOC_INSTALL_DIR=%{?_pkgdocdir} \
       -DCMAKE_BUILD_TYPE=RelWithDebInfo \
       -DUSE_SETUP_PY=0 \
       -DQD_DOC_INSTALL_DIR=%{_pkgdocdir} \
       -DBUILD_DOCS=ON \
       -DCMAKE_SKIP_RPATH:BOOL=OFF \
       -DUSE_LIBWEBSOCKETS=ON \
       -DCONSOLE_INSTALL=OFF \
      "-DCMAKE_CXX_FLAGS=$CXXFLAGS -Wno-error=use-after-free -Wno-error=array-bounds " \
      "-DCMAKE_C_FLAGS=$CFLAGS -Wno-error=maybe-uninitialized -Wno-error=unused-variable -Wno-error=unused-function " \
       .
make
make doc


%install
%make_install
install -dm 755 %{buildroot}%{_unitdir}
install -pm 644 %{_builddir}/qpid-dispatch-%{version}/etc/fedora/qdrouterd.service \
                %{buildroot}%{_unitdir}
install -dm 755 %{buildroot}/var/run/qpid-dispatch
install -dm 755 %{buildroot}%{_pkglicensedir}
install -pm 644 %{SOURCE1} %{buildroot}%{_pkglicensedir}
install -pm 644 %{buildroot}%{_pkgdocdir}/LICENSE %{buildroot}%{_pkglicensedir}
rm -f %{buildroot}%{_pkgdocdir}/LICENSE

if [[ %{_console_subpackage} != 0 ]]; then
install -dm 755 %{buildroot}/%{_datarootdir}/qpid-dispatch/console
cp -a %{_builddir}/qpid-dispatch-%{version}/pre_built/console/* %{buildroot}/%{_datarootdir}/qpid-dispatch/console/
fi

rm -f  %{buildroot}/%{_includedir}/qpid/dispatch.h
rm -fr %{buildroot}/%{_includedir}/qpid/dispatch

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig


%changelog
* Fri Jan 26 2024 Fedora Release Engineering <releng@fedoraproject.org> - 1.19.0-9
- Rebuilt for https://fedoraproject.org/wiki/Fedora_40_Mass_Rebuild

* Mon Jan 22 2024 Fedora Release Engineering <releng@fedoraproject.org> - 1.19.0-8
- Rebuilt for https://fedoraproject.org/wiki/Fedora_40_Mass_Rebuild

* Tue Nov 28 2023 Irina Boverman <iboverma@redhat.com> - 1.19.0-7
- Added patch to resolve bz 2245601

* Mon Jul 31 2023 Irina Boverman <iboverma@redhat.com> - 1.19.0-6
- Added patch to resolve bz 2226386

* Fri Jul 21 2023 Fedora Release Engineering <releng@fedoraproject.org> - 1.19.0-6
- Rebuilt for https://fedoraproject.org/wiki/Fedora_39_Mass_Rebuild

* Mon Jul 10 2023 Python Maint <python-maint@redhat.com> - 1.19.0-5
- Rebuilt for Python 3.12

* Fri Jan 20 2023 Fedora Release Engineering <releng@fedoraproject.org> - 1.19.0-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_38_Mass_Rebuild

* Fri Jul 22 2022 Fedora Release Engineering <releng@fedoraproject.org> - 1.19.0-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_37_Mass_Rebuild

* Tue Jun 21 2022 Irina Boverman <iboverma@redhat.com> - 1.19.0-2
- Rebuild to resolve install issue
- Rebuilt for Python 3.11

* Tue Mar 29 2022 Kim van der Riet <kvanderr@redhat.com> - 1.19.0 - 1
- Rebased to 1.19.0

* Wed Feb 23 2022 Mamoru TASAKA <mtasaka@fedoraproject.org> - 1.18.0-3
- rebuild for new libwebsockets

* Thu Jan 27 2022 Irina Boverman <iboverma@redhat.com> - 1.18.0 - 2
- Changed cmake flags

* Fri Jan 21 2022 Fedora Release Engineering <releng@fedoraproject.org> - 1.18.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_36_Mass_Rebuild

* Tue Jan 11 2022 Irina Boverman <iboverma@redhat.com> - 1.18.0 - 1
- Rebased to 1.18.0

* Thu Sep  2 2021 Irina Boverman <iboverma@redhat.com> - 1.17.0 - 1
- Rebased to 1.17.0

* Fri Jul 23 2021 Fedora Release Engineering <releng@fedoraproject.org> - 1.16.1-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_35_Mass_Rebuild

* Tue Jul 13 2021 Kim van der Riet <kvanderr@redhat.com> - 1.16.1-1
- Rebased to 1.16.1

* Fri Jun 04 2021 Python Maint <python-maint@redhat.com> - 1.16.0-2
- Rebuilt for Python 3.10

* Mon May 17 2021 Irina Boverman <iboverma@redhat.com> - 1.16.0-1
- Rebased to 1.16.0

* Tue Mar 23 2021 Kim van der Riet <kvanderr@redhat.com> - 1.15.0-1
- Rebased to 1.15.0

* Tue Mar 02 2021 Zbigniew Jędrzejewski-Szmek <zbyszek@in.waw.pl> - 1.14.0-3
- Rebuilt for updated systemd-rpm-macros
  See https://pagure.io/fesco/issue/2583.

* Wed Jan 27 2021 Fedora Release Engineering <releng@fedoraproject.org> - 1.14.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_34_Mass_Rebuild

* Wed Sep 16 2020 Irina Boverman <iboverma@redhat.com> - 1.14.0-1
- Rebased to 1.14.0

* Sat Aug 01 2020 Fedora Release Engineering <releng@fedoraproject.org> - 1.12.0-4
- Second attempt - Rebuilt for
  https://fedoraproject.org/wiki/Fedora_33_Mass_Rebuild

* Wed Jul 29 2020 Fedora Release Engineering <releng@fedoraproject.org> - 1.12.0-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_33_Mass_Rebuild

* Tue May 26 2020 Miro Hrončok <mhroncok@redhat.com> - 1.12.0-2
- Rebuilt for Python 3.9

* Thu May 14 2020 Irina Boverman <iboverma@redhat.com> - 1.12.0-1
- Rebased to 1.12.0

* Mon Apr 13 2020 Irina Boverman <iboverma@redhat.com> - 1.11.0-1
- Rebased to 1.11.0

* Tue Feb  4 2020 Irina Boverman <iboverma@redhat.com> - 1.10.0-1
- Rebased to 1.10.0

* Thu Oct 03 2019 Miro Hrončok <mhroncok@redhat.com> - 1.9.0-2
- Rebuilt for Python 3.8.0rc1 (#1748018)

* Wed Oct  2 2019 Irina Boverman <iboverma@redhat.com> - 1.9.0-1
- Rebased to 1.9.0

* Mon Aug 19 2019 Miro Hrončok <mhroncok@redhat.com> - 1.8.0-3
- Rebuilt for Python 3.8

* Fri Jul 26 2019 Fedora Release Engineering <releng@fedoraproject.org> - 1.8.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_31_Mass_Rebuild

* Thu Jun 20 2019 Irina Boverman <iboverma@redhat.com> - 1.8.0-1
- Rebased to 1.8.0

* Tue May 14 2019 Irina Boverman <iboverma@redhat.com> - 1.7.0-1
- Rebased to 1.7.0

* Sat Feb 02 2019 Fedora Release Engineering <releng@fedoraproject.org> - 1.5.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_30_Mass_Rebuild

* Mon Jan 14 2019 Irina Boverman <iboverma@redhat.com> - 1.5.0-1
- Rebased to 1.5.0

* Tue Jan  8 2019 Irina Boverman <iboverma@redhat.com> - 1.4.1-1
- Rebased to 1.4.1

* Tue Aug 21 2018 Irina Boverman <iboverma@redhat.com> - 1.2.0-3
- Added DISPATCH-1091 fix

* Tue Jul 31 2018 Florian Weimer <fweimer@redhat.com> - 1.2.0-2
- Rebuild with fixed binutils

* Fri Jul 27 2018 Irina Boverman <iboverma@redhat.com> - 1.2.0-1
- Rebased to 1.2.0
- Added DISPATCH-1087 patch

* Sat Jul 14 2018 Fedora Release Engineering <releng@fedoraproject.org> - 1.0.1-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_29_Mass_Rebuild

* Tue Mar 13 2018 Irina Boverman <iboverma@redhat.com> - 1.0.1-1
- Rebased to 1.0.1

* Wed Feb 28 2018 Iryna Shcherbina <ishcherb@redhat.com> - 1.0.0-3
- Update Python 2 dependency declarations to new packaging standards
  (See https://fedoraproject.org/wiki/FinalizingFedoraSwitchtoPython3)

* Fri Feb 09 2018 Igor Gnatenko <ignatenkobrain@fedoraproject.org> - 1.0.0-2
- Escape macros in %%changelog
* Tue Nov 21 2017 Irina Boverman <iboverma@redhat.com> - 1.0.0-1
- Rebased to 1.0.0
- Added DISPATCH-881 fix

* Thu Nov 16 2017 Irina Boverman <iboverma@redhat.com> - 0.8.0-7
- Rebuilt against qpid-proton 0.18.1

* Sat Oct 21 2017 Irina Boverman <iboverma@redhat.com> - 0.8.0-6
- Rebuilt to fix broken dependencies

* Mon Aug 14 2017 Irina Boverman <iboverma@redhat.com> - 0.8.0-5
- Added fix for DISPATCH-727

* Mon Aug 14 2017 Fedora Release Engineering <releng@fedoraproject.org> - 0.8.0-4
- Rebuilt against latest version of libwebsockets

* Thu Aug 03 2017 Fedora Release Engineering <releng@fedoraproject.org> - 0.8.0-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_27_Binutils_Mass_Rebuild

* Thu Jul 27 2017 Fedora Release Engineering <releng@fedoraproject.org> - 0.8.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_27_Mass_Rebuild

* Thu May 25 2017 Irina Boverman <iboverma@redhat.com> - 0.8.0-1
- Rebased to 0.8.0

* Wed Feb 22 2017  Irina Boverman <iboverma@redhat.com> - 0.7.0-1
- Rebased to 0.7.0
- Rebuilt against qpid-proton 0.17.0

* Sat Feb 11 2017 Fedora Release Engineering <releng@fedoraproject.org> - 0.6.1-5
- Rebuilt for https://fedoraproject.org/wiki/Fedora_26_Mass_Rebuild

* Wed Feb  1 2017 Irina Boverman <iboverma@redhat.com> - 0.6.1-4
- Updated "Requires: python-qpid-proton" to use >= %%{proton_minimum_version}
* Thu Sep  8 2016 Irina Boverman <iboverma@redhat.com> - 0.6.1-3
- Rebuilt against qpid-proton 0.14.0

* Tue Aug 23 2016 Irina Boverman <iboverma@redhat.com> - 0.6.1-2
- Obsoleted libqpid-dispatch-devel

* Wed Aug 17 2016 Irina Boverman <iboverma@redhat.com> - 0.6.1-1
- Rebased to 0.6.1
- Corrected doc package build process

* Tue Jul 19 2016 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.6.0-2
- https://fedoraproject.org/wiki/Changes/Automatic_Provides_for_Python_RPM_Packages

* Fri Jun 24 2016 Irina Boverman <iboverma@redhat.com> - 0.6.0-1
- Rebased to 0.6.0
- Rebuilt against qpid-proton 0.13.0-1
- Changed qpid-dispatch-router-docs to qpid-dispatch-docs

* Wed Mar 23 2016 Irina Boverman <iboverma@redhat.com> - 0.5-3
- Rebuilt against proton 0.12.1

* Thu Feb 04 2016 Fedora Release Engineering <releng@fedoraproject.org> - 0.5-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_24_Mass_Rebuild

* Wed Sep 16 2015 Irina Boverman <iboverma@redhat.com> - 0.5-1
- Rebased to qpid dispatch 0.5

* Thu Jun 18 2015 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.4-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_23_Mass_Rebuild

* Wed May 27 2015 Darryl L. Pierce <dpierce@redhat.com> - 0.4-2
- Create the local state directory explicity on SysVInit systems.

* Tue Apr 21 2015 Darryl L. Pierce <dpierce@rehdat.com> - 0.4-1
- Rebased on Dispatch 0.4.
- Changed username for qdrouterd to be qdrouterd.

* Tue Feb 24 2015 Darryl L. Pierce <dpierce@redhat.com> - 0.3-4
- Changed SysVInit script to properly name qdrouterd as the service to start.

* Fri Feb 20 2015 Darryl L. Pierce <dpierce@redhat.com> - 0.3-3
- Update inter-package dependencies to include release as well as version.

* Wed Feb 11 2015 Darryl L. Pierce <dpierce@redhat.com> - 0.3-2
- Disabled building documentation due to missing pandoc-pdf on EL6.
- Disabled daemon setgid.
- Fixes to accomodate Python 2.6 on EL6.
- Removed implicit dependency on python-qpid-proton in qpid-dispatch-router.

* Tue Jan 27 2015 Darryl L. Pierce <dpierce@redhat.com> - 0.3-1
- Rebased on Dispatch 0.3.
- Increased the minimum Proton version needed to 0.8.
- Moved all tests to the -devel package.
- Ensure executable bit turned off on systemd file.
- Set the location of installed documentation.

* Thu Nov 20 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-9
- Fixed a merge issue that resulted in two patches not being applied.
- Resolves: BZ#1165691

* Wed Nov 19 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-8
- DISPATCH-75 - Removed reference to qdstat.conf from qdstat manpage.
- Include systemd service file for EPEL7 packages.
- Brought systemd support up to current Fedora packaging guidelines.
- Resolves: BZ#1165691
- Resolves: BZ#1165681

* Sun Aug 17 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.2-7
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_22_Mass_Rebuild

* Wed Jul  9 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-6
- Removed intro-package comments which can cause POSTUN warnings.
- Added dependency on libqpid-dispatch from qpid-dispatch-tools.

* Wed Jul  2 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-5
- Fixed the path for the configuration file.
- Resolves: BZ#1115416

* Sun Jun 08 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.2-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_Mass_Rebuild

* Fri May 30 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-3
- Fixed build type to be RelWithDebInfo

* Tue Apr 22 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-2
- Fixed merging problems across Fedora and EPEL releases.

* Tue Apr 22 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.2-1
- Rebased on Qpid Dispatch 0.2.

* Wed Feb  5 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.1-4
- Fixed path to configuration in qpid-dispatch.service file.
- Added requires from qpid-dispatch-tools to python-qpid-proton.

* Thu Jan 30 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.1-3
- Fix build system to not discard CFLAGS provided by Fedora
- Resolves: BZ#1058448
- Simplified the specfile to be used across release targets.

* Fri Jan 24 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.1-2
- First release for Fedora.
- Resolves: BZ#1055721

* Thu Jan 23 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.1-1.2
- Put all subpackage sections above prep/build/install.
- Removed check and clean sections.
- Added remaining systemd macros.
- Made qpid-dispatch-router-docs a noarch package.

* Wed Jan 22 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.1-1.1
- Added the systemd macros for post/preun/postun
- Moved prep/build/install/check/clean above package definitions.

* Mon Jan 20 2014 Darryl L. Pierce <dpierce@redhat.com> - 0.1-1
- Initial packaging of the codebase.

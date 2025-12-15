# HexChat Windows Meson Migration Plan

## Overview

This document outlines the plan for migrating the HexChat Windows build from Visual Studio `.sln`/`.vcxproj` files to a unified Meson build system. This would allow using the same build system across all platforms (Linux, macOS, Windows).

## Current Windows Build State

### Visual Studio Build Structure

**Solution File:** `win32/hexchat.sln`
- Visual Studio 2022 (v143 toolset)
- Supports Release|Win32 and Release|x64 configurations

**Project Files (19 total):**

| Location | Project | Type |
|----------|---------|------|
| `src/common/common.vcxproj` | Core library | Static lib |
| `src/fe-gtk/fe-gtk.vcxproj` | GTK GUI | Executable |
| `src/fe-text/fe-text.vcxproj` | Text frontend | Executable |
| `src/fe-gtk/notifications/notifications-winrt.vcxproj` | WinRT notifications | DLL (C++) |
| `src/libenchant_win8/libenchant_win8.vcxproj` | Spell check wrapper | DLL |
| `plugins/checksum/checksum.vcxproj` | DCC checksum | Plugin DLL |
| `plugins/exec/exec.vcxproj` | /exec command | Plugin DLL (Win-only) |
| `plugins/fishlim/fishlim.vcxproj` | FiSH encryption | Plugin DLL |
| `plugins/lua/lua.vcxproj` | Lua scripting | Plugin DLL |
| `plugins/perl/perl.vcxproj` | Perl scripting | Plugin DLL |
| `plugins/python/python3.vcxproj` | Python 3 scripting | Plugin DLL |
| `plugins/sasl/sasl.vcxproj` | SASL auth | Plugin DLL |
| `plugins/sysinfo/sysinfo.vcxproj` | System info | Plugin DLL |
| `plugins/upd/upd.vcxproj` | Update checker | Plugin DLL (Win-only) |
| `plugins/winamp/winamp.vcxproj` | Winamp integration | Plugin DLL (Win-only) |
| `win32/nls/nls.vcxproj` | Localization | Utility |
| `win32/copy/copy.vcxproj` | Copy artifacts | Utility |
| `win32/installer/installer.vcxproj` | Inno Setup | Utility |

**Configuration Files:**
- `win32/hexchat.props` - Shared MSBuild properties (dependency paths, compiler flags)
- `win32/Directory.Build.props` - Wildcard item support
- `win32/version-template.ps1` - Version stamping script

### Current Meson Windows Support

The existing `meson.build` has partial Windows support:

```meson
# Already implemented:
if host_machine.system() == 'windows'
  add_project_arguments('-DWIN32', '-DNTDDI_VERSION=NTDDI_WIN7', ...)
endif

# MSVC-specific dependency handling:
if cc.get_id() == 'msvc'
  libssl_dep = cc.find_library('libssl')
endif

# Skips data/po dirs for MSVC:
if cc.get_id() != 'msvc'
  subdir('data')
  subdir('po')
endif
```

---

## Migration Challenges

### 1. Dependency Discovery (Medium Effort)

**Current VS Approach:**
```xml
<!-- win32/hexchat.props -->
<YourDepsPath>c:\gtk-build\gtk</YourDepsPath>
<DepsRoot>$(YourDepsPath)\$(PlatformName)\release</DepsRoot>
<DepLibs>gtk-3.lib;gdk-3.lib;glib-2.0.lib;...</DepLibs>
```

**Meson Options:**

| Approach | Pros | Cons |
|----------|------|------|
| **MSYS2/MinGW** | pkg-config works natively, pre-built GTK3/4 | Different ABI than MSVC |
| **vcpkg** | MSVC-native, Meson integration | May need custom portfiles |
| **Manual find_library** | Works with existing gvsbuild | Verbose, must list all deps |
| **Wrap files** | Self-contained builds | High setup effort |

**Recommended:** Start with MSYS2/MinGW for simplicity, then add MSVC support.

### 2. Resource Compilation (Medium Effort)

**Current VS Approach:**
```xml
<PreBuildEvent>
  powershell -File "version-template.ps1" "hexchat.rc.tt" "hexchat.rc"
  glib-compile-resources.exe --generate-source ...
</PreBuildEvent>
```

**Meson Solution:**
```meson
windows = import('windows')

# Version templating
hexchat_rc = configure_file(
  input: 'hexchat.rc.in',
  output: 'hexchat.rc',
  configuration: {
    'VERSION': meson.project_version(),
    'VERSION_COMMA': meson.project_version().replace('.', ',')
  }
)

# Compile resources
rc_file = windows.compile_resources(hexchat_rc)

# GResources (already implemented)
resources = gnome.compile_resources(...)
```

### 3. Scripting Language Plugins (High Effort)

**Current VS Paths:**
```xml
<YourPerlPath>C:\Strawberry\perl</YourPerlPath>
<YourPython3Path>C:\Users\...\Python313</YourPython3Path>
<LuaInclude>$(DepsRoot)\include\luajit-2.1</LuaInclude>
```

**Meson Solution:**
```meson
# Python
python = import('python')
py_installation = python.find_installation('python3', required: get_option('with-python'))
if py_installation.found()
  py_dep = py_installation.dependency(embed: true)
endif

# Perl - complex on Windows
perl_dep = dependency('perl', required: false)
if not perl_dep.found() and host_machine.system() == 'windows'
  # Try to find Strawberry Perl
  perl_inc = include_directories('C:/Strawberry/perl/lib/CORE')
  perl_lib = cc.find_library('perl542', dirs: 'C:/Strawberry/perl/lib/CORE')
  perl_dep = declare_dependency(include_directories: perl_inc, dependencies: perl_lib)
endif

# Lua/LuaJIT
luajit_dep = dependency('luajit', required: get_option('with-lua'))
```

### 4. WinRT Notifications (Medium Effort)

**Current:** Separate C++ project (`notifications-winrt.vcxproj`)

**Meson Solution:**
```meson
if host_machine.system() == 'windows' and cc.get_id() == 'msvc'
  add_languages('cpp')

  winrt_sources = ['notifications/notification-winrt.cpp']

  shared_library('hcnotifications-winrt',
    sources: winrt_sources,
    cpp_args: ['/ZW'],  # Enable C++/CX
    install: true,
    install_dir: get_option('libdir') / 'hexchat'
  )
endif
```

### 5. Installer Generation (Low Effort)

**Current:** `installer.vcxproj` runs Inno Setup

**Meson Solution:**
```meson
if host_machine.system() == 'windows'
  inno_setup = find_program('iscc', required: false)

  if inno_setup.found()
    # Generate .iss from template
    iss_file = configure_file(
      input: 'win32/installer/hexchat.iss.in',
      output: 'hexchat.iss',
      configuration: config_h
    )

    # Custom target (not built by default)
    custom_target('installer',
      command: [inno_setup, '@INPUT@'],
      input: iss_file,
      output: 'HexChat-@0@.exe'.format(meson.project_version()),
      build_by_default: false
    )
  endif
endif
```

### 6. Missing POSIX Headers (Low Effort)

**Current:** `src/dirent/dirent-win32.h` provides `dirent.h` compatibility

**Meson Solution:**
```meson
if host_machine.system() == 'windows'
  dirent_inc = include_directories('src/dirent')
  # Add to relevant targets
endif
```

---

## Implementation Plan

### Phase 1: MinGW/MSYS2 Support (Easier Path)

**Goal:** Get HexChat building with Meson + MinGW-w64 toolchain

**Steps:**
1. Install MSYS2 with mingw-w64-x86_64-gtk3 (or gtk4) package
2. Update `meson.build` to handle MinGW quirks
3. Add Windows resource compilation via `windows.compile_resources()`
4. Test basic build without scripting plugins
5. Add GitHub Actions workflow for MinGW builds

**Estimated Time:** 2-3 days

### Phase 2: Core MSVC Support (Harder Path)

**Goal:** Build HexChat with native MSVC using gvsbuild dependencies

**Steps:**
1. Create dependency wrapper for gvsbuild packages:
   ```meson
   # Option A: Manual find_library for each
   gtk_dep = cc.find_library('gtk-3', dirs: deps_lib_dir)
   glib_dep = cc.find_library('glib-2.0', dirs: deps_lib_dir)
   # ... etc for all deps

   # Option B: Generate .pc files for gvsbuild
   # Option C: Use subproject wraps
   ```
2. Handle include paths for all dependencies
3. Add resource file compilation
4. Test with existing gvsbuild artifacts

**Estimated Time:** 3-5 days

### Phase 3: Scripting Plugins

**Goal:** Python, Perl, and Lua plugin support

**Steps:**
1. Add Python plugin build (use python module)
2. Add Lua plugin build (find LuaJIT)
3. Add Perl plugin build (complex - may need custom detection)
4. Test each plugin loads and works

**Estimated Time:** 2-3 days

### Phase 4: Advanced Features

**Goal:** Feature parity with VS build

**Steps:**
1. Add WinRT notification backend (requires C++)
2. Add libenchant_win8 wrapper build
3. Add NLS/localization support
4. Add installer generation (Inno Setup)
5. Update CI to use Meson for Windows

**Estimated Time:** 3-4 days

### Phase 5: Cleanup & Documentation

**Goal:** Remove VS files, document new process

**Steps:**
1. Update README with Windows Meson instructions
2. Update CI workflows
3. (Optional) Remove VS solution and project files
4. Test clean builds on fresh Windows system

**Estimated Time:** 1-2 days

---

## Meson Code Examples

### Main meson.build Windows Additions

```meson
# Near the top
if host_machine.system() == 'windows'
  windows_mod = import('windows')

  # For MSVC with gvsbuild
  if cc.get_id() == 'msvc'
    deps_root = get_option('deps-root')  # e.g., 'c:/gtk-build/gtk/x64/release'
    deps_inc = include_directories(deps_root / 'include')
    deps_lib = deps_root / 'lib'
  endif
endif
```

### fe-gtk/meson.build Windows Additions

```meson
# Resource file for Windows
if host_machine.system() == 'windows'
  version_parts = meson.project_version().split('.')

  rc_config = configuration_data()
  rc_config.set('VERSION', meson.project_version())
  rc_config.set('VERSION_MAJOR', version_parts[0])
  rc_config.set('VERSION_MINOR', version_parts[1])
  rc_config.set('VERSION_PATCH', version_parts[2])

  hexchat_rc = configure_file(
    input: 'hexchat.rc.in',
    output: 'hexchat.rc',
    configuration: rc_config
  )

  hexchat_gtk_sources += windows_mod.compile_resources(hexchat_rc)
endif
```

### New meson_options.txt Entries

```meson
# Windows-specific options
option('deps-root', type: 'string', value: '',
       description: 'Path to gvsbuild dependency root (MSVC only)')

option('with-winrt-notifications', type: 'boolean', value: true,
       description: 'Build WinRT notification backend (Windows only)')

option('with-installer', type: 'boolean', value: false,
       description: 'Build Inno Setup installer (Windows only)')
```

---

## Dependencies Reference

### Required Windows Libraries

| Library | Purpose | gvsbuild Package |
|---------|---------|------------------|
| gtk-3.lib / gtk-4.lib | GTK toolkit | gtk3 / gtk4 |
| gdk-3.lib / gdk-4.lib | GDK | gtk3 / gtk4 |
| glib-2.0.lib | GLib | glib |
| gio-2.0.lib | GIO | glib |
| gobject-2.0.lib | GObject | glib |
| gmodule-2.0.lib | GModule | glib |
| gdk_pixbuf-2.0.lib | Image loading | gdk-pixbuf |
| pango-1.0.lib | Text rendering | pango |
| pangocairo-1.0.lib | Pango Cairo | pango |
| pangowin32-1.0.lib | Pango Win32 | pango |
| cairo.lib | 2D graphics | cairo |
| libssl.lib | TLS | openssl |
| libcrypto.lib | Crypto | openssl |
| xml2.lib | XML parsing | libxml2 |
| intl.lib | Gettext | gettext |

### System Libraries (Windows SDK)

- `ws2_32.lib` - Winsock
- `winmm.lib` - Multimedia (sound)
- `wbemuuid.lib` - WMI
- `wininet.lib` - Internet utilities
- `comsupp.lib` - COM support

### Optional Libraries

| Library | Purpose | Required For |
|---------|---------|--------------|
| lua51.lib | LuaJIT | Lua plugin |
| perl5XX.lib | Perl | Perl plugin |
| python3XX.lib | Python | Python plugin |
| WinSparkle.lib | Auto-update | Update checker |

---

## CI/CD Changes

### Current GitHub Actions (VS Build)

```yaml
- name: Build
  run: |
    msbuild win32\hexchat.sln /m /p:Configuration=Release /p:Platform=${{ matrix.platform }}
```

### New GitHub Actions (Meson Build)

```yaml
# Option A: MSYS2/MinGW
- uses: msys2/setup-msys2@v2
  with:
    msystem: MINGW64
    install: >-
      mingw-w64-x86_64-gtk3
      mingw-w64-x86_64-meson
      mingw-w64-x86_64-ninja
      mingw-w64-x86_64-openssl

- name: Build
  shell: msys2 {0}
  run: |
    meson setup build
    meson compile -C build

# Option B: MSVC with gvsbuild
- name: Setup gvsbuild
  run: |
    # Download pre-built deps
    Invoke-WebRequest $GTK_URL -OutFile gtk.7z
    7z x gtk.7z -oC:\gtk-build

- name: Build
  run: |
    meson setup build --backend=vs2022 -Ddeps-root=C:\gtk-build\gtk\x64\release
    meson compile -C build
```

---

## Timeline Summary

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: MinGW Support | 2-3 days | None |
| Phase 2: MSVC Support | 3-5 days | Phase 1 |
| Phase 3: Scripting Plugins | 2-3 days | Phase 2 |
| Phase 4: Advanced Features | 3-4 days | Phase 3 |
| Phase 5: Cleanup | 1-2 days | Phase 4 |
| **Total** | **~2-3 weeks** | |

---

## Benefits of Migration

1. **Single build system** - Same commands on Linux, macOS, Windows
2. **Easier maintenance** - No duplicate file lists in VS projects
3. **Better CI/CD** - Simpler workflow configuration
4. **GTK4 ready** - Meson is GTK's recommended build system
5. **Modern tooling** - Better dependency management, faster builds
6. **Cross-compilation** - Easier to target different architectures

## Risks

1. **Build breakage** - Initial migration may introduce bugs
2. **Performance differences** - MSVC vs MinGW optimization
3. **Dependency complexity** - Finding libraries on Windows is harder
4. **WinRT complications** - C++/CX requires MSVC

---

## References

- [Meson Manual - Windows](https://mesonbuild.com/Running-Meson.html#windows)
- [Meson Windows Module](https://mesonbuild.com/Windows-module.html)
- [MSYS2 Project](https://www.msys2.org/)
- [gvsbuild](https://github.com/wingtk/gvsbuild) - GTK for Windows
- [vcpkg](https://vcpkg.io/) - C++ package manager

---

*Document created: 2024-12-14*
*Last updated: 2024-12-14*

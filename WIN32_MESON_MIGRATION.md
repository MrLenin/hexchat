# HexChat Windows Meson Build Support

## Overview

This document outlines the plan for adding Meson build support on Windows **alongside** the existing Visual Studio solution. The goal is to enable Meson builds without breaking the current VS workflow.

**Current Status:** Planning phase - no implementation started

## Goals

1. **Feature parity**: Match VS build capabilities with Meson
2. **GTK4 support**: Work with both GTK3 and GTK4
3. **Non-breaking transition**: Keep VS solution functional until Meson is verified working
4. **Eventually remove VS**: Once Meson is proven, delete `win32/*.sln`, `win32/*.props`, and all `*.vcxproj` files

## Current Build Systems

### Visual Studio (Primary - Working)

**Solution:** `win32/hexchat.sln` (Visual Studio 2022, v143 toolset)

**Configuration:**
- `win32/hexchat.props` - Dependency paths, compiler flags (currently GTK4)
- `win32/hexchat-gtk3.props` - GTK3 configuration (backup)
- `win32/Directory.Build.props` - Wildcard support

**Projects (19 total):**
| Project | Type | Notes |
|---------|------|-------|
| `src/common/common.vcxproj` | Static lib | Core IRC library |
| `src/fe-gtk/fe-gtk.vcxproj` | Executable | Main GUI |
| `src/fe-text/fe-text.vcxproj` | Executable | Text frontend |
| `notifications-winrt.vcxproj` | DLL (C++) | Windows notifications |
| `libenchant_win8.vcxproj` | DLL | Spell check |
| 8 plugin projects | Plugin DLLs | Lua, Perl, Python, etc. |
| 3 utility projects | Various | NLS, copy, installer |

### Meson (Partial - Linux/MinGW focused)

**Files:**
- `meson.build` - Main build file
- `meson_options.txt` - Build options
- `src/meson.build`, `src/common/meson.build`, `src/fe-gtk/meson.build`
- `plugins/*/meson.build`

**Current Windows support:**
- Basic `WIN32` defines and Windows-specific flags
- MinGW linker flags (`-Wl,--nxcompat`, etc.)
- Partial MSVC detection (`cc.get_id() == 'msvc'`)
- GTK4 option (`-Dgtk4=true`)

**Missing for MSVC:**
- Resource file compilation (.rc)
- Dependency discovery for gvsbuild packages
- WinRT notification backend
- Windows-specific plugins (exec, upd, winamp)
- Installer generation

---

## Implementation Plan

### Phase 1: MSVC Dependency Discovery

**Goal:** Enable `meson setup` to find gvsbuild dependencies

**Approach:** Add a `deps-root` option pointing to gvsbuild output

```meson
# meson_options.txt addition
option('deps-root', type: 'string', value: '',
       description: 'Path to gvsbuild dependency root (Windows/MSVC only)')
```

```meson
# meson.build addition
if host_machine.system() == 'windows' and cc.get_id() == 'msvc'
  deps_root = get_option('deps-root')
  if deps_root != ''
    deps_inc = include_directories(deps_root / 'include')
    deps_lib_dir = deps_root / 'lib'

    # Manual library discovery
    gtk_dep = declare_dependency(
      include_directories: [
        include_directories(deps_root / 'include' / 'gtk-4.0'),
        include_directories(deps_root / 'include' / 'glib-2.0'),
        # ... more includes
      ],
      dependencies: [
        cc.find_library('gtk-4', dirs: deps_lib_dir),
        cc.find_library('glib-2.0', dirs: deps_lib_dir),
        # ... more libs
      ]
    )
  endif
endif
```

**Usage:**
```bash
meson setup build --backend=vs2022 \
  -Ddeps-root=c:/gtk-build/gtk4/x64/release \
  -Dgtk4=true
```

### Phase 2: Resource File Compilation

**Goal:** Compile Windows .rc files for version info and icons

```meson
# src/fe-gtk/meson.build addition
if host_machine.system() == 'windows'
  windows_mod = import('windows')

  # Version template processing
  version_parts = meson.project_version().split('.')
  rc_config = configuration_data()
  rc_config.set('VERSION', meson.project_version())
  rc_config.set('VERSION_MAJOR', version_parts[0])
  rc_config.set('VERSION_MINOR', version_parts[1])
  rc_config.set('VERSION_PATCH', version_parts.get(2, '0'))
  rc_config.set('VERSION_COMMA', ','.join(version_parts))

  hexchat_rc = configure_file(
    input: 'hexchat.rc.in',
    output: 'hexchat.rc',
    configuration: rc_config
  )

  hexchat_gtk_sources += windows_mod.compile_resources(hexchat_rc)
endif
```

**Requires:** Creating `hexchat.rc.in` template from existing `hexchat.rc.tt`

### Phase 3: Core Build (common + fe-gtk)

**Goal:** Build hexchat.exe with MSVC via Meson

**Tasks:**
1. Add all Windows-specific source files
2. Handle dirent compatibility header
3. Add required Windows SDK libraries (ws2_32, winmm, etc.)
4. Test with gvsbuild GTK4

**Verification:**
```bash
meson compile -C build
# Should produce build/src/fe-gtk/hexchat.exe
```

### Phase 4: Plugin Builds

**Goal:** Build core plugins with Meson

**Priority order:**
1. checksum, fishlim, sysinfo (no external deps)
2. lua (needs LuaJIT)
3. python (needs Python embed)
4. perl (complex - Strawberry Perl detection)
5. Windows-only: exec, upd, winamp

```meson
# plugins/meson.build Windows additions
if host_machine.system() == 'windows'
  # Lua plugin
  luajit_inc = include_directories(deps_root / 'include' / 'luajit-2.1')
  luajit_lib = cc.find_library('lua51', dirs: deps_lib_dir)

  # Python plugin
  py_inc = include_directories(get_option('python-path') / 'include')
  py_lib = cc.find_library('python313', dirs: get_option('python-path') / 'libs')
endif
```

### Phase 5: Advanced Features

**Goal:** Feature parity with VS build

**Tasks:**
1. WinRT notification backend (requires C++/CX)
2. libenchant_win8 wrapper
3. Update checker plugin (WinSparkle)
4. Installer generation (Inno Setup)

### Phase 6: Verification and Cleanup

**Goal:** Remove VS solution after Meson is proven working

**Verification Checklist:**
- [ ] hexchat.exe builds and runs
- [ ] All plugins build and load
- [ ] GTK3 build works
- [ ] GTK4 build works
- [ ] Installer generates correctly
- [ ] CI/CD pipeline passes

**Cleanup Tasks:**
1. Delete `win32/hexchat.sln`
2. Delete `win32/*.props` files
3. Delete all `*.vcxproj` and `*.vcxproj.filters` files
4. Update README with Meson-only build instructions
5. Remove VS-specific CI workflows

---

## File Changes Required

### New Files

| File | Purpose |
|------|---------|
| `src/fe-gtk/hexchat.rc.in` | Resource file template |
| `.github/workflows/meson-windows.yml` | CI workflow |

### Modified Files

| File | Changes |
|------|---------|
| `meson_options.txt` | Add `deps-root`, `python-path`, `perl-path` options |
| `meson.build` | MSVC dependency handling, Windows SDK libs |
| `src/fe-gtk/meson.build` | Resource compilation, Windows sources |
| `plugins/*/meson.build` | Windows-specific plugin builds |

### Unchanged Files (Until Migration Complete)

| File | Notes |
|------|-------|
| `win32/hexchat.sln` | Keep until Meson verified |
| `win32/hexchat.props` | Keep until Meson verified |
| All `*.vcxproj` files | Keep until Meson verified |

### Files to Delete (After Migration Verified)

| Files | Count |
|-------|-------|
| `win32/*.sln` | 1 |
| `win32/*.props` | 2-3 |
| `src/**/*.vcxproj` | ~10 |
| `plugins/**/*.vcxproj` | ~10 |
| `win32/**/*.vcxproj` | ~3 |

---

## Dependencies Reference

### gvsbuild Packages Required

```
gtk4 (or gtk3)
glib
pango
cairo
gdk-pixbuf
openssl
libxml2
gettext
luajit (for Lua plugin)
```

### Windows SDK Libraries

```
ws2_32.lib    - Winsock
winmm.lib     - Multimedia (sound)
wininet.lib   - Internet utilities
wbemuuid.lib  - WMI (sysinfo)
comsupp.lib   - COM support
```

### Scripting Interpreters

| Interpreter | Windows Source |
|-------------|----------------|
| LuaJIT | gvsbuild or manual |
| Python | python.org installer |
| Perl | Strawberry Perl |

---

## Build Instructions (Future)

### Prerequisites

1. Visual Studio 2022 with C++ workload
2. Meson and Ninja (`pip install meson ninja`)
3. gvsbuild GTK4 packages in `c:\gtk-build\gtk4`
4. (Optional) Strawberry Perl, Python for plugins

### Build Commands

```bash
# Configure (generates VS solution)
meson setup build --backend=vs2022 \
  -Ddeps-root=c:/gtk-build/gtk4/x64/release \
  -Dgtk4=true

# Build
meson compile -C build

# Or open in Visual Studio
start build/hexchat.sln
```

### Parallel with Existing VS Build

The Meson build outputs to `build/` directory, completely separate from the existing:
- `win32/hexchat.sln` - Original VS solution
- `hexchat-build-gtk4/` - Original VS output directory

Both can coexist without conflict.

---

## Timeline Estimate

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: Dependency discovery | 1-2 days | None |
| Phase 2: Resource compilation | 1 day | Phase 1 |
| Phase 3: Core build | 2-3 days | Phase 2 |
| Phase 4: Plugins | 2-3 days | Phase 3 |
| Phase 5: Advanced features | 3-4 days | Phase 4 |
| Phase 6: Verification & cleanup | 1-2 days | Phase 5 |
| **Total** | **~2-3 weeks** | |

---

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Dependency discovery complexity | Start with hardcoded paths, refine later |
| WinRT requires MSVC C++/CX | Keep as optional, fall back to basic notifications |
| Plugin interpreter paths vary | Use meson options for custom paths |
| VS and Meson configs diverge | Document which is authoritative |

---

## References

- [Meson Windows Module](https://mesonbuild.com/Windows-module.html)
- [gvsbuild](https://github.com/wingtk/gvsbuild)
- [Meson MSVC Support](https://mesonbuild.com/Using-with-Visual-Studio.html)

---

*Document created: 2024-12-14*
*Last updated: 2024-12-15*

project(
  'OpenPrey',
  ['c', 'cpp'],
  version: '0.0.1',
  meson_version: '>=1.2.0',
  default_options: ['c_std=c11', 'cpp_std=c++20', 'warning_level=0', 'b_vscrt=static_from_buildtype'],
)

host_system = host_machine.system()
host_cpu_family = host_machine.cpu_family()
if not ['windows', 'linux', 'darwin'].contains(host_system)
  error('Unsupported host system for OpenPrey Meson build: ' + host_system)
endif

binary_arch = host_cpu_family
if host_cpu_family == 'x86_64'
  binary_arch = 'x64'
elif host_cpu_family == 'x86'
  binary_arch = 'x86'
elif host_cpu_family == 'aarch64'
  binary_arch = 'arm64'
endif
game_binary_name = 'game_' + binary_arch
engine_client_binary_name = meson.project_name() + '-client_' + binary_arch
engine_ded_binary_name = meson.project_name() + '-ded_' + binary_arch

if host_cpu_family != 'x86_64'
  warning(
    'OpenPrey currently targets x64 as the active compatibility baseline. '
    + 'Non-x64 builds are considered experimental during platform bring-up.'
  )
endif

platform_backend_requested = get_option('platform_backend')
platform_backend = platform_backend_requested
if host_system == 'windows' and platform_backend_requested == 'native'
  error('platform_backend=native is only valid on non-Windows hosts.')
endif

if host_system != 'windows'
  if platform_backend_requested == 'legacy_win32'
    error('platform_backend=legacy_win32 is only valid on Windows hosts.')
  endif

  if platform_backend_requested == 'sdl3'
    message(
      'platform_backend=sdl3 on non-Windows currently runs in staging mode and maps to native platform sources. '
      + 'Use this to keep command lines/CI aligned while SDL3 migration is in progress.'
    )
  endif
  platform_backend = 'native'
endif

use_sdl3_backend = host_system == 'windows' and platform_backend_requested == 'sdl3'
if host_system == 'windows' and not use_sdl3_backend
  warning(
    'legacy_win32 backend is transitional. '
    + 'Use -Dplatform_backend=sdl3 for portability-focused development.'
  )
endif

python_mod = import('python')
fs_mod = import('fs')
py = python_mod.find_installation('python3', required: true)
if host_system == 'darwin'
  add_languages('objc', 'objcpp', required: true)
endif

build_engine = get_option('build_engine')
build_games = get_option('build_games')
legacy_build_game_sp = get_option('build_game_sp')
legacy_build_game_mp = get_option('build_game_mp')
install_root_dir = meson.project_source_root() / '.install'
install_gamedir_dir = install_root_dir / 'basepy'
game_module_defs_file = meson.project_source_root() / 'src' / 'game' / 'Game.def'

if legacy_build_game_sp or legacy_build_game_mp
  warning(
    'build_game_sp/build_game_mp are legacy no-op options. '
    + 'OpenPrey now builds a unified game module (game_<arch>) when build_games=true.'
  )
endif

if not build_engine and not build_games
  error('At least one build target must be enabled. Use -Dbuild_engine=true and/or -Dbuild_games=true.')
endif

engine_source_discovery = run_command(
  py,
  meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
  '--host-system',
  host_system,
  '--platform-backend',
  platform_backend,
  '--include-game',
  'false',
  check: true,
)

engine_source_paths = engine_source_discovery.stdout().strip().split('\n')
if engine_source_paths.length() == 0
  error('tools/build/meson_sources.py returned no engine source files.')
endif

engine_sources = files(engine_source_paths)

game_sources = []
game_idlib_sources = []
if build_games
  game_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/game',
    'src/game/Callbacks.cpp',
    'src/game/gamesys/Callbacks.cpp',
    check: true,
  )
  game_source_paths = game_source_discovery.stdout().strip().split('\n')
  if game_source_paths.length() == 0
    error('tools/build/list_sources.py returned no game source files.')
  endif

  prey_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/Prey',
    'src/Prey/ai_Navigator.cpp',
    'src/Prey/game_events.cpp',
    'src/Prey/particles_particles.cpp',
    'src/Prey/prey_weapon.cpp',
    check: true,
  )
  prey_source_paths = prey_source_discovery.stdout().strip().split('\n')
  if prey_source_paths.length() == 0
    error('tools/build/list_sources.py returned no Prey source files.')
  endif

  game_sources = files(game_source_paths + prey_source_paths)

  game_idlib_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
    meson.project_source_root(),
    'src/idlib',
    'src/idlib/math/Simd_AltiVec.cpp',
    check: true,
  )
  game_idlib_paths = game_idlib_discovery.stdout().strip().split('\n')
  if game_idlib_paths.length() == 0
    error('tools/build/list_sources.py returned no game idlib source files.')
  endif
  game_idlib_sources = files(game_idlib_paths)
endif

root_include_dir = include_directories('.')
resource_compiler = find_program('rc', 'windres', required: false)
if host_system == 'windows' and resource_compiler.found()
  windows_mod = import('windows')
  engine_sources += windows_mod.compile_resources(
    'src/sys/win32/rc/OpenPreyVersion.rc',
    include_directories: [root_include_dir],
  )
endif

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

is_msvc = cc.get_id() == 'msvc' and cpp.get_id() == 'msvc'
msvc_2026_min = '19.46.0'
if is_msvc
  if cpp.version().version_compare('<' + msvc_2026_min)
    msvc_2026_msg = 'MSVC 19.46+ (Visual Studio 2026 toolset baseline) is recommended. Detected: ' + cpp.version() + '.'
    if get_option('enforce_msvc_2026')
      error(msvc_2026_msg)
    else
      warning(msvc_2026_msg + ' Configure with -Denforce_msvc_2026=true to enforce this baseline.')
    endif
  endif
elif get_option('enforce_msvc_2026')
  warning('enforce_msvc_2026 is only meaningful when using the MSVC toolchain.')
endif

fallback_opt = ['default_library=static']

glew_dep = disabler()
stb_vorbis_dep = disabler()
openal_dep = disabler()
sdl3_dep = disabler()
deps = []
if build_engine
  if host_system == 'windows'
    glew_dep = dependency(
      'glew',
      required: true,
      fallback: ['glew', 'glew_dep'],
      default_options: fallback_opt,
    )
  else
    glew_dep = dependency('glew', required: true)
  endif

  stb_vorbis_dep = dependency(
    'stb_vorbis',
    required: true,
    fallback: ['stb_vorbis', 'stb_vorbis_dep'],
    default_options: fallback_opt,
  )

  if host_system == 'darwin'
    openal_dep = dependency('appleframeworks', modules: ['OpenAL'], required: true)
  else
    openal_dep = dependency(
      'openal',
      required: true,
      fallback: ['openal-soft-prebuilt', 'openal_dep'],
      default_options: fallback_opt,
    )
  endif

  if use_sdl3_backend
    sdl3_dep = dependency(
      'sdl3',
      version: '>=3.4.0',
      required: true,
      fallback: ['sdl3', 'sdl3_dep'],
      default_options: fallback_opt,
    )
  endif

  deps = [glew_dep, openal_dep, stb_vorbis_dep]

  if host_system == 'windows'
    deps += [
      cc.find_library('opengl32', required: true),
      cc.find_library('Dbghelp', required: true),
      cc.find_library('Iphlpapi', required: true),
      cc.find_library('Winmm', required: true),
      cc.find_library('ws2_32', required: true),
    ]

    if use_sdl3_backend
      deps += [sdl3_dep]
    else
      deps += [
        cc.find_library('dinput8', required: true),
        cc.find_library('dxguid', required: true),
      ]
    endif
  elif host_system == 'linux'
    deps += [
      dependency('threads', required: true),
      cc.find_library('dl', required: true),
      dependency('x11', required: true),
      dependency('xext', required: true),
      dependency('xxf86vm', required: true),
      cc.find_library('GL', required: true),
    ]
  elif host_system == 'darwin'
    deps += [
      dependency('threads', required: true),
      dependency(
        'appleframeworks',
        modules: ['Cocoa', 'OpenGL', 'ApplicationServices', 'Carbon'],
        required: true,
      ),
    ]
  endif
endif

shared_cpp_args = ['-DUSE_OPENAL', '-DGLEW_NO_GLU', '-DHUMANHEAD=1']
shared_c_args = ['-DGLEW_NO_GLU', '-DHUMANHEAD=1']
engine_link_args = []

if host_system == 'windows'
  shared_cpp_args += ['-D_WINDOWS', '-DWIN32']
  shared_c_args += ['-D_WINDOWS', '-DWIN32']
  engine_link_args += ['/STACK:16777216,16777216']
else
  shared_cpp_args += ['-DID_GL_HARDLINK']
  shared_c_args += ['-DID_GL_HARDLINK']
endif

if host_system == 'darwin'
  shared_cpp_args += ['-DMACOS_X=1']
  shared_c_args += ['-DMACOS_X=1']
endif

if use_sdl3_backend
  shared_cpp_args += ['-DUSE_SDL3=1']
  shared_c_args += ['-DUSE_SDL3=1']
endif

if cpp.get_argument_syntax() == 'msvc'
  shared_cpp_args += ['/J', '/FS', '/source-charset:windows-1252', '/execution-charset:windows-1252', '/Zc:strictStrings-', '/W0']
  shared_c_args += ['/FS', '/source-charset:windows-1252', '/execution-charset:windows-1252', '/W0']
else
  shared_cpp_args += ['-fpermissive']
endif

pch_headers = []
if get_option('use_pch')
  pch_headers = ['src/idlib/precompiled.h']
endif
game_idlib_pch_headers = ['src/idlib/precompiled.h']
game_module_pch_headers = ['../src/idlib/precompiled.h']

src_include_dir = include_directories('src')
engine_include_dirs = [root_include_dir, src_include_dir]
game_include_dirs = [root_include_dir, src_include_dir]

engine_cpp_args = shared_cpp_args + ['-D__DOOM_DLL__']
engine_c_args = shared_c_args

if build_engine
  if host_system == 'windows'
    executable(
      engine_client_binary_name,
      engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      win_subsystem: 'windows',
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  elif host_system == 'darwin'
    executable(
      engine_client_binary_name,
      engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: engine_cpp_args,
      c_args: engine_c_args,
      objcpp_args: engine_cpp_args,
      objc_args: engine_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  else
    executable(
      engine_client_binary_name,
      engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: engine_cpp_args,
      c_args: engine_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  endif

  dedicated_cpp_args = engine_cpp_args + ['-DID_DEDICATED']
  dedicated_c_args = engine_c_args + ['-DID_DEDICATED']

  if host_system == 'windows'
    executable(
      engine_ded_binary_name,
      engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      cpp_pch: pch_headers,
      win_subsystem: 'windows',
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  elif host_system == 'darwin'
    executable(
      engine_ded_binary_name,
      engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      objcpp_args: dedicated_cpp_args,
      objc_args: dedicated_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  else
    executable(
      engine_ded_binary_name,
      engine_sources,
      include_directories: engine_include_dirs,
      dependencies: deps,
      cpp_args: dedicated_cpp_args,
      c_args: dedicated_c_args,
      cpp_pch: pch_headers,
      link_args: engine_link_args,
      install: true,
      install_dir: install_root_dir,
    )
  endif

  if host_system == 'windows' and host_cpu_family == 'x86_64'
    openal_runtime_candidates = [
      meson.project_source_root() / 'src' / 'external' / 'openal-soft' / 'bin' / 'win64' / 'OpenAL32.dll',
      meson.project_source_root() / 'subprojects' / 'openal-soft-prebuilt' / 'bin' / 'win64' / 'OpenAL32.dll',
    ]
    openal_runtime_staged = false
    foreach openal_runtime_dll : openal_runtime_candidates
      if fs_mod.is_file(openal_runtime_dll)
        install_data(
          openal_runtime_dll,
          install_dir: install_root_dir,
        )
        openal_runtime_staged = true
        break
      endif
    endforeach
    if not openal_runtime_staged
      warning(
        'OpenAL Soft runtime DLL was not found in expected locations; '
        + 'runtime audio will fall back to the system OpenAL32.dll.'
      )
    endif
  endif
endif

game_common_cpp_args = shared_cpp_args + ['-DGAME_DLL']
game_common_c_args = shared_c_args
game_module_link_args = []
if host_system == 'windows'
  game_module_link_args = ['/FIXED:NO', '/LARGEADDRESSAWARE']
  if host_cpu_family == 'x86_64'
    game_module_link_args += ['/BASE:0x180000000']
  else
    game_module_link_args += ['/BASE:0x20000000']
  endif
  release_like = ['release', 'debugoptimized', 'minsize'].contains(get_option('buildtype'))
  if release_like
    game_module_link_args += ['/STACK:4194304', '/OPT:REF', '/OPT:ICF', '/MAP']
  else
    game_module_link_args += ['/STACK:16000000,16000000']
  endif
endif

game_idlib_library = disabler()
if build_games
  game_idlib_library = static_library(
    'openprey_game_idlib',
    game_idlib_sources,
    include_directories: game_include_dirs,
    cpp_args: game_common_cpp_args,
    c_args: game_common_c_args,
    cpp_pch: game_idlib_pch_headers,
    install: false,
  )
endif

if build_engine or build_games
  subdir('basepy')
endif

install_data(
  'assets/icons/prey.ico',
  install_dir: install_root_dir,
  rename: 'OpenPrey.ico',
)

install_data(
  'assets/icons/prey.icns',
  install_dir: install_root_dir,
  rename: 'OpenPrey.icns',
)

if host_system == 'linux'
  linux_desktop_conf = configuration_data()
  linux_desktop_conf.set('OPENPREY_CLIENT_BINARY', engine_client_binary_name)
  linux_desktop_conf.set('OPENPREY_PROJECT_VERSION', meson.project_version())
  linux_desktop_conf.set('OPENPREY_PROJECT_VERSION_FULL', meson.project_version())
  linux_desktop_entry = configure_file(
    input: 'assets/linux/openprey.desktop.in',
    output: 'openprey.desktop',
    configuration: linux_desktop_conf,
  )
  install_data(
    linux_desktop_entry,
    install_dir: install_root_dir / 'share' / 'applications',
  )

  if fs_mod.is_file('assets/icons/prey_256.svg')
    install_data(
      'assets/icons/prey_256.svg',
      install_dir: install_root_dir / 'share' / 'icons' / 'hicolor' / 'scalable' / 'apps',
      rename: 'openprey.svg',
    )
  endif

  icon_sizes = ['16', '24', '32', '48', '64', '128', '256']
  foreach icon_size : icon_sizes
    icon_png = 'assets/icons/prey_' + icon_size + '.png'
    if fs_mod.is_file(icon_png)
      install_data(
        icon_png,
        install_dir: install_root_dir / 'share' / 'icons' / 'hicolor' / (icon_size + 'x' + icon_size) / 'apps',
        rename: 'openprey.png',
      )
    endif
  endforeach
endif

summary(
  {
    'Host system': host_system,
    'Host cpu family': host_cpu_family,
    'MSVC toolchain': is_msvc,
    'MSVC version': cpp.version(),
    'MSVC 2026 baseline enforced': get_option('enforce_msvc_2026'),
    'C++ standard': get_option('cpp_std'),
    'Platform backend requested': platform_backend_requested,
    'Platform backend': platform_backend,
    'Build engine': build_engine,
    'engine client binary': engine_client_binary_name,
    'engine dedicated binary': engine_ded_binary_name,
    'Build games': build_games,
    'Unified game binary': build_games ? game_binary_name : '',
    'Resource compiler': resource_compiler.found(),
    'SDL3': use_sdl3_backend and sdl3_dep.found(),
    'GLEW': glew_dep.found(),
    'OpenAL': openal_dep.found(),
    'stb_vorbis': stb_vorbis_dep.found(),
    'Precompiled headers': get_option('use_pch'),
    'Dedicated server target': build_engine,
    'Install root': install_root_dir,
  },
  section: 'Build Features',
  bool_yn: true,
)

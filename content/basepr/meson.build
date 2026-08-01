if build_games
  if host_system == 'windows'
    game_module_target = shared_module(
      game_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args,
      vs_module_defs: game_module_defs_file,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  elif host_system == 'darwin'
    game_module_target = shared_library(
      game_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args + ['-Wl,-install_name,@loader_path/' + game_binary_name + '.dylib'],
      link_depends: game_module_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: '',
      name_suffix: 'dylib',
      install: true,
      install_dir: install_game_dir,
    )
  else
    game_module_target = shared_module(
      game_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      override_options: game_target_override_options,
      link_args: game_module_link_args,
      link_depends: game_module_link_depends,
      gnu_symbol_visibility: 'hidden',
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  endif
endif

basepr_manifest_conf = configuration_data()
basepr_manifest_conf.set('OPENPREY_VERSION_BASE', openq4_version_base)
basepr_manifest = configure_file(
  input: 'mod.json.in',
  output: 'mod.json',
  configuration: basepr_manifest_conf,
)

install_data(
  basepr_manifest,
  install_dir: install_game_dir,
)

custom_target(
  'openprey_stage_mod_manifest_direct_run',
  input: basepr_manifest,
  output: 'stage-mod-manifest.stamp',
  command: [
    py,
    meson.project_source_root() / 'tools' / 'build' / 'stage_direct_run_game_module.py',
    '--source',
    '@INPUT@',
    '--destination',
    meson.project_build_root() / 'basepr' / 'mod.json',
    '--stamp',
    '@OUTPUT@',
  ],
  depend_files: files('../../tools/build/stage_direct_run_game_module.py'),
  build_always_stale: true,
  build_by_default: true,
)

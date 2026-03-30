if build_games
  if host_system == 'windows'
    shared_module(
      game_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      vs_module_defs: game_module_defs_file,
      name_prefix: '',
      install: true,
      install_dir: install_gamedir_dir,
    )
  else
    shared_module(
      game_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_gamedir_dir,
    )
  endif
endif

install_subdir(
  'materials',
  install_dir: install_gamedir_dir,
)

install_subdir(
  'glprogs',
  install_dir: install_gamedir_dir,
)

install_subdir(
  'guis',
  install_dir: install_gamedir_dir,
  exclude_directories: ['assets'],
)

install_data(
  [
    'guis/assets/menu/background_left.tga',
    'guis/assets/menu/background_right.tga',
    'guis/assets/menu/background_top.tga',
    'guis/assets/menu/background_bottom.tga',
  ],
  install_dir: install_gamedir_dir / 'guis' / 'assets' / 'menu',
)

install_data(
  [
    'guis/assets/loading/loading.tga',
    'guis/assets/loading/roadhouse.tga',
    'guis/assets/loading/loading_left.tga',
    'guis/assets/loading/loading_right.tga',
    'guis/assets/loading/loading_top.tga',
    'guis/assets/loading/loading_bottom.tga',
    'guis/assets/loading/roadhouse_left.tga',
    'guis/assets/loading/roadhouse_right.tga',
    'guis/assets/loading/roadhouse_top.tga',
    'guis/assets/loading/roadhouse_bottom.tga',
  ],
  install_dir: install_gamedir_dir / 'guis' / 'assets' / 'loading',
)

install_subdir(
  'maps',
  install_dir: install_gamedir_dir,
)

install_subdir(
  'script',
  install_dir: install_gamedir_dir,
)

install_subdir(
  'strings',
  install_dir: install_gamedir_dir,
)

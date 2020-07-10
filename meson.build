project('myfs', 'c', default_options : ['c_std=c11'])
if get_option('buildtype').startswith('debug')
  add_project_arguments('-DDEBUG', language : 'c')
endif

fusedep = dependency('fuse3')

executable('mkfs.myfs', 'myfs.c', 'mkfs.c')
executable('fsinfo', 'myfs.c', 'fsinfo.c')
executable('myfs', 'myfs.c', 'main.c', dependencies : fusedep)

executable('fstest', 'myfs.c', 'test.c')

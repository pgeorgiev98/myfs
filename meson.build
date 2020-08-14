project('myfs', 'c', default_options : ['c_std=c11'])
if get_option('buildtype').startswith('debug')
  add_project_arguments('-DDEBUG', language : 'c')
endif

if host_machine.endian() == 'little'
  add_project_arguments('-DLITTLE_ENDIAN=1', language : 'c')
else
  add_project_arguments('-DLITTLE_ENDIAN=0', language : 'c')
endif

fusedep = dependency('fuse3')

executable('mkfs.myfs', 'myfs.c', 'mkfs.c', 'helpers.c')
executable('fsinfo', 'myfs.c', 'fsinfo.c', 'helpers.c')
executable('myfs', 'myfs.c', 'main.c', 'helpers.c', 'inode_map.c', dependencies : fusedep)

executable('fstest', 'myfs.c', 'test.c', 'helpers.c')

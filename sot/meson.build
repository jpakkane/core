sot_lib = shared_library('sotlo',
  'source/unoolestorage/xolesimplestorage.cxx',
  'source/base/formats.cxx',
  'source/base/object.cxx',
  'source/base/exchange.cxx',
  'source/base/filelist.cxx',
  'source/sdstor/stg.cxx',
  'source/sdstor/stgavl.cxx',
  'source/sdstor/stgcache.cxx',
  'source/sdstor/stgdir.cxx',
  'source/sdstor/stgelem.cxx',
  'source/sdstor/stgio.cxx',
  'source/sdstor/stgole.cxx',
  'source/sdstor/stgstrms.cxx',
  'source/sdstor/storage.cxx',
  'source/sdstor/storinfo.cxx',
  'source/sdstor/ucbstorage.cxx',
  udk_fake_h, off_fake_h,
  cpp_args: ['-DTOOLS_DLLIMPLEMENTATION',
             '-DLIBO_INTERNAL_ONLY'],
  include_directories: [main_inc, udkh_inc, offapi_inc, 'inc'],
  link_with: [comphelper_lib, cppuhelper_lib, cppu_lib, salhelper_lib, sal_lib, i18nlangtag_lib, tl_lib, ucbhelper_lib, utl_lib],
  gnu_symbol_visibility: 'hidden',
  install: true,
)

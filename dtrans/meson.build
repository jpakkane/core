mcnttype_lib = shared_library('mcnttype',
  'source/cnttype/mcnttfactory.cxx',
  'source/cnttype/mcnttype.cxx',
  'source/cnttype/mctfentry.cxx',
  udk_fake_h, off_fake_h, common_hxx,
  cpp_args: ['-DLIBO_INTERNAL_ONLY'],
  include_directories: [main_inc, udkh_inc, offapi_inc, setup_inc],
  link_with: [cppu_lib,
    cppuhelper_lib,
    sal_lib,
    tl_lib,
  ],
  dependencies: [],
  gnu_symbol_visibility: 'hidden',
  install: true,
)

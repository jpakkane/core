oopathutils_lib = static_library('oopathutils',
  'source/misc/pathutils.cxx',
  include_directories: [main_inc],
)

tl_lib = shared_library('tllo',
  'source/datetime/datetime.cxx',
  'source/datetime/datetimeutils.cxx',
  'source/datetime/systemdatetime.cxx',
  'source/datetime/tdate.cxx',
  'source/datetime/ttime.cxx',
  'source/debug/debug.cxx',
  'source/fsys/fileutil.cxx',
  'source/fsys/urlobj.cxx',
  'source/fsys/wldcrd.cxx',
  'source/generic/b3dtrans.cxx',
  'source/generic/bigint.cxx',
  'source/generic/color.cxx',
  'source/generic/config.cxx',
  'source/generic/fract.cxx',
  'source/generic/gen.cxx',
  'source/generic/line.cxx',
  'source/generic/point.cxx',
  'source/generic/poly.cxx',
  'source/generic/poly2.cxx',
  'source/generic/svborder.cxx',
  'source/inet/inetmime.cxx',
  'source/inet/inetmsg.cxx',
  'source/inet/inetstrm.cxx',
  'source/memtools/multisel.cxx',
  'source/misc/cpuid.cxx',
  'source/misc/extendapplicationenvironment.cxx',
  'source/ref/globname.cxx',
  'source/ref/ref.cxx',
  'source/stream/stream.cxx',
  'source/stream/vcompat.cxx',
  'source/stream/GenericTypeSerializer.cxx',
  'source/stream/strmunx.cxx',
  'source/string/tenccvt.cxx',
  'source/zcodec/zcodec.cxx',
  'source/xml/XmlWriter.cxx',
  'source/xml/XmlWalker.cxx',
  # FIXME: create this file at build time
  # rather than using this pregenerated one.
  # The executable crashes on startup if run
  # directly, it requires some env setup
  # that I'm not aware of.
  'reversemap.cxx',
  udk_fake_h, off_fake_h,
  cpp_args: ['-DTOOLS_DLLIMPLEMENTATION',
             '-DLIBO_INTERNAL_ONLY'],
  include_directories: [main_inc, udkh_inc, offapi_inc, 'inc'],
  link_with: [basegfx_lib, comphelper_lib, i18nlangtag_lib, cppu_lib, cppuhelper_lib, sal_lib],
  dependencies: [libxml_dep, zlib_dep],
  gnu_symbol_visibility: 'hidden',
  install: true,
)

bestreversemap_exe = executable('bestreversemap',
  'source/reversemap/bestreversemap.cxx',
  include_directories: [main_inc],
  link_with: [sal_lib],
  install: true,
)

# FIXME, add custom target to generate reverse map.

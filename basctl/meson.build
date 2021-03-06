basslots_hxx = custom_target('basslots',
  input: ['sdi/basslots.sdi'],
  output: ['basslots.hxx'],
  depfile: '@BASENAME@.d',
  command: [svidl_bin,
            '-quiet',
            '-I' + meson.current_source_dir() + '/sdi',
            '-I' + meson.source_root() + '/svx/sdi',
            '-I' + meson.source_root() + '/sfx2/sdi',
            '-I' + meson.source_root() + '/include',
            '-fx' + meson.current_source_dir() + '/sdi/baside.sdi',
            '-fs' + meson.build_root() + '/@OUTPUT@',
            '-fM' + meson.build_root() + '/@DEPFILE@',
            '-fm' + meson.current_build_dir(),
            meson.build_root() + '/@INPUT@'
            ]
)

basctl_lib = shared_library('basctl',
  'source/accessibility/accessibledialogcontrolshape.cxx',
  'source/accessibility/accessibledialogwindow.cxx',
  'source/basicide/basdoc.cxx',
  'source/basicide/IDEComboBox.cxx',
  'source/basicide/basicrenderable.cxx',
  'source/basicide/baside2b.cxx',
  'source/basicide/baside2.cxx',
  'source/basicide/baside3.cxx',
  'source/basicide/basidectrlr.cxx',
  'source/basicide/basides1.cxx',
  'source/basicide/basides2.cxx',
  'source/basicide/basides3.cxx',
  'source/basicide/basidesh.cxx',
  'source/basicide/basobj2.cxx',
  'source/basicide/basobj3.cxx',
  'source/basicide/bastype2.cxx',
  'source/basicide/bastype3.cxx',
  'source/basicide/bastypes.cxx',
  'source/basicide/breakpoint.cxx',
  'source/basicide/brkdlg.cxx',
  'source/basicide/doceventnotifier.cxx',
  'source/basicide/docsignature.cxx',
  'source/basicide/documentenumeration.cxx',
  'source/basicide/iderdll.cxx',
  'source/basicide/layout.cxx',
  'source/basicide/linenumberwindow.cxx',
  'source/basicide/localizationmgr.cxx',
  'source/basicide/macrodlg.cxx',
  'source/basicide/moduldl2.cxx',
  'source/basicide/moduldlg.cxx',
  'source/basicide/ObjectCatalog.cxx',
  'source/basicide/register.cxx',
  'source/basicide/sbxitem.cxx',
  'source/basicide/scriptdocument.cxx',
  'source/basicide/unomodel.cxx',
  'source/dlged/dlgedclip.cxx',
  'source/dlged/dlged.cxx',
  'source/dlged/dlgedfac.cxx',
  'source/dlged/dlgedfunc.cxx',
  'source/dlged/dlgedlist.cxx',
  'source/dlged/dlgedmod.cxx',
  'source/dlged/dlgedobj.cxx',
  'source/dlged/dlgedpage.cxx',
  'source/dlged/dlgedview.cxx',
  'source/dlged/managelang.cxx',
  'source/dlged/propbrw.cxx',
  basslots_hxx,
  udk_fake_h, off_fake_h, common_hxx, basicide_hxx,
  cpp_args: ['-DLIBO_INTERNAL_ONLY'],
  include_directories: [main_inc, udkh_inc, offapi_inc, 'inc', 'source/inc', setup_inc],
  link_with: [
    comphelper_lib,
    cppu_lib,
    cppuhelper_lib,
    editeng_lib,
    fwe_lib,
    sal_lib,
    i18nlangtag_lib,
    sb_lib,
    sfx_lib,
    sot_lib,
    svl_lib,
    svt_lib,
    svx_lib,
    svxcore_lib,
    tk_lib,
    tl_lib,
    utl_lib,
    ucbhelper_lib,
    vcl_lib,
    xmlscript_lib,
  ],
  dependencies: [],
  install: true,
)

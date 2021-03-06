subdir('Office')
subdir('TypeDetection')

setup_hxx = custom_target('Setup.hxx',
  output: 'Setup.hxx',
  input: ['registry/cppheader.xsl',
          'registry/schema/org/openoffice/Setup.xcs'],
  command: [xslt_bin,
            '--nonet',
            '--stringparam',
            'ns1',
            'Setup',
            '-o',
            '@OUTPUT@',
            '@INPUT@'])

system_hxx = custom_target('System.hxx',
  output: 'System.hxx',
  input: ['registry/cppheader.xsl',
          'registry/schema/org/openoffice/System.xcs'],
  command: [xslt_bin,
            '--nonet',
            '--stringparam',
            'ns1',
            'System',
            '-o',
            '@OUTPUT@',
            '@INPUT@'])

vcl_hxx = custom_target('VCL.hxx',
  output: 'VCL.hxx',
  input: ['registry/cppheader.xsl',
          'registry/schema/org/openoffice/VCL.xcs'],
  command: [xslt_bin,
            '--nonet',
            '--stringparam',
            'ns1',
            'VCL',
            '-o',
            '@OUTPUT@',
            '@INPUT@'])

inet_hxx = custom_target('Inet.hxx',
  output: 'Inet.hxx',
  input: ['registry/cppheader.xsl',
          'registry/schema/org/openoffice/Inet.xcs'],
  command: [xslt_bin,
            '--nonet',
            '--stringparam',
            'ns1',
            'Inet',
            '-o',
            '@OUTPUT@',
            '@INPUT@'])

setup_inc = include_directories('..') # HACK

registry_en_us_xcu = custom_target('registry_en_US',
  output: 'registry_en-US.xcd',
  input: [
    'registry/data/org/openoffice/Setup.xcu',
    'registry/data/org/openoffice/Office/Accelerators.xcu',
    'registry/data/org/openoffice/Office/Addons.xcu',
    'registry/data/org/openoffice/Office/Common.xcu',
    'registry/data/org/openoffice/Office/DataAccess.xcu',
    'registry/data/org/openoffice/Office/PresentationMinimizer.xcu',
    'registry/data/org/openoffice/Office/PresenterScreen.xcu',
    'registry/data/org/openoffice/Office/TableWizard.xcu',
    'registry/data/org/openoffice/Office/UI.xcu',
    'registry/data/org/openoffice/Office/Embedding.xcu',
    'registry/data/org/openoffice/Office/ExtendedColorScheme.xcu',
    'registry/data/org/openoffice/Office/FormWizard.xcu',
    'registry/data/org/openoffice/Office/Writer.xcu',
    'registry/data/org/openoffice/Office/UI/BasicIDECommands.xcu',
    'registry/data/org/openoffice/Office/UI/BibliographyCommands.xcu',
    'registry/data/org/openoffice/Office/UI/CalcCommands.xcu',
    'registry/data/org/openoffice/Office/UI/ChartCommands.xcu',
    'registry/data/org/openoffice/Office/UI/ChartWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbuCommands.xcu',
    'registry/data/org/openoffice/Office/UI/BaseWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/WriterFormWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/WriterReportWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbQueryWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbTableWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbRelationWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbReportWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbBrowserWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DbTableDataWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DrawImpressCommands.xcu',
    'registry/data/org/openoffice/Office/UI/Effects.xcu',
    'registry/data/org/openoffice/Office/UI/GenericCommands.xcu',
    'registry/data/org/openoffice/Office/UI/MathCommands.xcu',
    'registry/data/org/openoffice/Office/UI/BasicIDEWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/CalcWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/DrawWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/ImpressWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/MathWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/ReportCommands.xcu',
    'registry/data/org/openoffice/Office/UI/Sidebar.xcu',
    'registry/data/org/openoffice/Office/UI/StartModuleWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/WriterWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/XFormsWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/WriterGlobalWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/WriterWebWindowState.xcu',
    'registry/data/org/openoffice/Office/UI/WriterCommands.xcu',
    'registry/data/org/openoffice/Office/UI/GenericCategories.xcu',
    'registry/data/org/openoffice/Office/UI/ToolbarMode.xcu',
  ],
  command: [find_program('genregistry.py'),
    meson.current_source_dir(),
    '@OUTPUT@',
    '@INPUT@',
  ],
  install: true,
  install_dir: get_option('datadir') / 'registry/res',
)
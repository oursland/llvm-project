add_lldb_library(lldbPluginABISysV_sh PLUGIN
  ABISysV_sh.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )

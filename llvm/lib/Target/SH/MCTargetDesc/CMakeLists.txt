add_llvm_component_library(LLVMSHDesc
  SHInstPrinter.cpp
  SHMCTargetDesc.cpp
  SHMCAsmInfo.cpp
  SHMCCodeEmitter.cpp
  SHAsmBackend.cpp
  SHELFObjectWriter.cpp

  LINK_COMPONENTS
  MC
  Support
  SHInfo

  ADD_TO_COMPONENT
  SH
  )

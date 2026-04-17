add_llvm_component_group(SH)

set(LLVM_TARGET_DEFINITIONS SH.td)

tablegen(LLVM SHGenRegisterInfo.inc     -gen-register-info)
tablegen(LLVM SHGenInstrInfo.inc        -gen-instr-info)
tablegen(LLVM SHGenSubtargetInfo.inc    -gen-subtarget)
tablegen(LLVM SHGenCallingConv.inc      -gen-callingconv)
tablegen(LLVM SHGenDAGISel.inc          -gen-dag-isel)
tablegen(LLVM SHGenAsmWriter.inc        -gen-asm-writer)
tablegen(LLVM SHGenMCCodeEmitter.inc    -gen-emitter)
tablegen(LLVM SHGenAsmMatcher.inc       -gen-asm-matcher)
tablegen(LLVM SHGenDisassemblerTables.inc -gen-disassembler)

add_public_tablegen_target(SHCommonTableGen)

add_llvm_target(SHCodeGen
  SHAsmPrinter.cpp
  SHFrameLowering.cpp
  SHInstrInfo.cpp
  SHISelDAGToDAG.cpp
  SHISelLowering.cpp
  SHMachineFunctionInfo.cpp
  SHMCInstLower.cpp
  SHRegisterInfo.cpp
  SHSelectionDAGInfo.cpp
  SHSubtarget.cpp
  SHTargetMachine.cpp
  SHTargetObjectFile.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  SHDesc
  SHInfo
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  SH
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)

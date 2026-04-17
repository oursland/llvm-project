//===-- ABISysV_sh.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_sh.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/TargetParser/Triple.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectMemory.h"
#include "lldb/ValueObject/ValueObjectRegister.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ABISysV_sh)

// DWARF layout as defined by SH GNU ABI
enum dwarf_regnums {
  dwarf_r0 = 0,
  dwarf_r1 = 1,
  dwarf_r2 = 2,
  dwarf_r3 = 3,
  dwarf_r4 = 4,
  dwarf_r5 = 5,
  dwarf_r6 = 6,
  dwarf_r7 = 7,
  dwarf_r8 = 8,
  dwarf_r9 = 9,
  dwarf_r10 = 10,
  dwarf_r11 = 11,
  dwarf_r12 = 12,
  dwarf_r13 = 13,
  dwarf_r14 = 14,
  dwarf_r15 = 15,
  dwarf_pc = 16,
  dwarf_pr = 17, // Link Register in SH
  dwarf_gbr = 18,
  dwarf_mach = 19,
  dwarf_macl = 20,
  dwarf_sr = 22,

  dwarf_fr0 = 25,
  dwarf_fr1 = 26,
  dwarf_fr2 = 27,
  dwarf_fr3 = 28,
  dwarf_fr4 = 29,
  dwarf_fr5 = 30,
  dwarf_fr6 = 31,
  dwarf_fr7 = 32,
  dwarf_fr8 = 33,
  dwarf_fr9 = 34,
  dwarf_fr10 = 35,
  dwarf_fr11 = 36,
  dwarf_fr12 = 37,
  dwarf_fr13 = 38,
  dwarf_fr14 = 39,
  dwarf_fr15 = 40,

  dwarf_xd0 = 41,
  dwarf_xd2 = 43,
  dwarf_xd4 = 45,
  dwarf_xd6 = 47,
  dwarf_xd8 = 49,
  dwarf_xd10 = 51,
  dwarf_xd12 = 53,
  dwarf_xd14 = 55,

  dwarf_fpul = 57,
  dwarf_fpscr = 58,
  dwarf_cfa,
};

// 32-bit width for general registers
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
      #reg,                                                                    \
      alt,                                                                     \
      4,                                                                       \
      0,                                                                       \
      eEncodingUint,                                                           \
      eFormatHex,                                                              \
      {kind1, kind2, kind3, kind4},                                            \
      nullptr,                                                                 \
      nullptr,                                                                 \
      nullptr,                                                                 \
  }

#define DEFINE_FPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
      #reg,                                                                    \
      alt,                                                                     \
      4,                                                                       \
      0,                                                                       \
      eEncodingIEEE754,                                                        \
      eFormatFloat,                                                            \
      {kind1, kind2, kind3, kind4},                                            \
      nullptr,                                                                 \
      nullptr,                                                                 \
      nullptr,                                                                 \
  }

static const RegisterInfo g_register_infos[] = {
    // General purpose registers.
    DEFINE_GPR(r0, nullptr, dwarf_r0, dwarf_r0, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r1, nullptr, dwarf_r1, dwarf_r1, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r2, nullptr, dwarf_r2, dwarf_r2, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r3, nullptr, dwarf_r3, dwarf_r3, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r4, nullptr, dwarf_r4, dwarf_r4, LLDB_REGNUM_GENERIC_ARG1,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r5, nullptr, dwarf_r5, dwarf_r5, LLDB_REGNUM_GENERIC_ARG2,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r6, nullptr, dwarf_r6, dwarf_r6, LLDB_REGNUM_GENERIC_ARG3,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r7, nullptr, dwarf_r7, dwarf_r7, LLDB_REGNUM_GENERIC_ARG4,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r8, nullptr, dwarf_r8, dwarf_r8, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r9, nullptr, dwarf_r9, dwarf_r9, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r10, nullptr, dwarf_r10, dwarf_r10, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11, nullptr, dwarf_r11, dwarf_r11, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r12, nullptr, dwarf_r12, dwarf_r12, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13, nullptr, dwarf_r13, dwarf_r13, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14, "fp", dwarf_r14, dwarf_r14, LLDB_REGNUM_GENERIC_FP,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15, "sp", dwarf_r15, dwarf_r15, LLDB_REGNUM_GENERIC_SP,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(pc, nullptr, dwarf_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(pr, "lr", dwarf_pr, dwarf_pr, LLDB_REGNUM_GENERIC_RA,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(gbr, nullptr, dwarf_gbr, dwarf_gbr, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(mach, nullptr, dwarf_mach, dwarf_mach, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(macl, nullptr, dwarf_macl, dwarf_macl, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(sr, nullptr, dwarf_sr, dwarf_sr, LLDB_REGNUM_GENERIC_FLAGS,
               LLDB_INVALID_REGNUM),

    // Floating point registers.
    DEFINE_FPR(fr0, nullptr, dwarf_fr0, dwarf_fr0, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr1, nullptr, dwarf_fr1, dwarf_fr1, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr2, nullptr, dwarf_fr2, dwarf_fr2, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr3, nullptr, dwarf_fr3, dwarf_fr3, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr4, nullptr, dwarf_fr4, dwarf_fr4, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr5, nullptr, dwarf_fr5, dwarf_fr5, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr6, nullptr, dwarf_fr6, dwarf_fr6, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr7, nullptr, dwarf_fr7, dwarf_fr7, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr8, nullptr, dwarf_fr8, dwarf_fr8, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr9, nullptr, dwarf_fr9, dwarf_fr9, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr10, nullptr, dwarf_fr10, dwarf_fr10, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr11, nullptr, dwarf_fr11, dwarf_fr11, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr12, nullptr, dwarf_fr12, dwarf_fr12, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr13, nullptr, dwarf_fr13, dwarf_fr13, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr14, nullptr, dwarf_fr14, dwarf_fr14, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_FPR(fr15, nullptr, dwarf_fr15, dwarf_fr15, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),

    DEFINE_GPR(fpul, nullptr, dwarf_fpul, dwarf_fpul, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
    DEFINE_GPR(fpscr, nullptr, dwarf_fpscr, dwarf_fpscr, LLDB_INVALID_REGNUM,
               LLDB_INVALID_REGNUM),
};

static const uint32_t k_num_register_infos = std::size(g_register_infos);

const lldb_private::RegisterInfo *
ABISysV_sh::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_sh::GetRedZoneSize() const {
  return 0;
} // No red zone in standard SH ABI

// Static Functions

ABISP
ABISysV_sh::CreateInstance(lldb::ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().isSH()) {
    return ABISP(
        new ABISysV_sh(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_sh::PrepareTrivialCall(Thread &thread, addr_t sp, addr_t func_addr,
                                    addr_t return_addr,
                                    llvm::ArrayRef<addr_t> args) const {
  Log *log = GetLog(LLDBLog::Expressions);

  if (log) {
    StreamString s;
    s.Printf("ABISysV_sh::PrepareTrivialCall (tid = 0x%" PRIx64
             ", sp = 0x%" PRIx64 ", func_addr = 0x%" PRIx64
             ", return_addr = 0x%" PRIx64,
             thread.GetID(), (uint64_t)sp, (uint64_t)func_addr,
             (uint64_t)return_addr);

    for (size_t i = 0; i < args.size(); ++i)
      s.Printf(", arg%" PRIu64 " = 0x%" PRIx64, static_cast<uint64_t>(i + 1),
               args[i]);
    s.PutCString(")");
    log->PutString(s.GetString());
  }

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return false;

  const RegisterInfo *reg_info = nullptr;

  if (args.size() >
      4) // Only supporting r4, r5, r6, r7 for now, stack arguments not handled
    return false;

  for (size_t i = 0; i < args.size(); ++i) {
    reg_info = reg_ctx->GetRegisterInfo(eRegisterKindGeneric,
                                        LLDB_REGNUM_GENERIC_ARG1 + i);
    LLDB_LOGF(log, "About to write arg%" PRIu64 " (0x%" PRIx64 ") into %s",
              static_cast<uint64_t>(i + 1), args[i], reg_info->name);
    if (!reg_ctx->WriteRegisterFromUnsigned(reg_info, args[i]))
      return false;
  }

  // Ensure SP is 4 byte aligned
  sp &= ~(3ull);

  const RegisterInfo *pc_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *sp_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
  const RegisterInfo *pr_reg_info =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA);

  LLDB_LOGF(log, "Writing PR (Return Address): 0x%" PRIx64,
            (uint64_t)return_addr);

  if (!reg_ctx->WriteRegisterFromUnsigned(pr_reg_info, return_addr))
    return false;

  LLDB_LOGF(log, "Writing SP: 0x%" PRIx64, (uint64_t)sp);

  if (!reg_ctx->WriteRegisterFromUnsigned(sp_reg_info, sp))
    return false;

  LLDB_LOGF(log, "Writing PC: 0x%" PRIx64, (uint64_t)func_addr);

  if (!reg_ctx->WriteRegisterFromUnsigned(pc_reg_info, func_addr))
    return false;

  return true;
}

static bool ReadIntegerArgument(Scalar &scalar, unsigned int bit_width,
                                bool is_signed, Thread &thread,
                                uint32_t *argument_register_ids,
                                unsigned int &current_argument_register,
                                addr_t &current_stack_argument) {
  if (bit_width > 32)
    return false; // Scalar can't handle long long easily unless we split. Just
                  // keeping simple.

  if (current_argument_register < 4) {
    scalar = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
        argument_register_ids[current_argument_register], 0);
    current_argument_register++;
    if (is_signed)
      scalar.SignExtend(bit_width);
  } else {
    uint32_t byte_size = (bit_width + (8 - 1)) / 8;
    Status error;
    if (thread.GetProcess()->ReadScalarIntegerFromMemory(
            current_stack_argument, byte_size, is_signed, scalar, error)) {
      current_stack_argument += 4; // SH stack slots are 4 bytes
      return true;
    }
    return false;
  }
  return true;
}

bool ABISysV_sh::GetArgumentValues(Thread &thread, ValueList &values) const {
  unsigned int num_values = values.GetSize();
  unsigned int value_index;

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();

  if (!reg_ctx)
    return false;

  addr_t sp = reg_ctx->GetSP(0);
  if (!sp)
    return false;

  addr_t current_stack_argument = sp;

  uint32_t argument_register_ids[4];

  argument_register_ids[0] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG1)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[1] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG2)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[2] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG3)
          ->kinds[eRegisterKindLLDB];
  argument_register_ids[3] =
      reg_ctx->GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_ARG4)
          ->kinds[eRegisterKindLLDB];

  unsigned int current_argument_register = 0;

  for (value_index = 0; value_index < num_values; ++value_index) {
    Value *value = values.GetValueAtIndex(value_index);

    if (!value)
      return false;

    CompilerType compiler_type = value->GetCompilerType();
    std::optional<uint64_t> bit_size =
        llvm::expectedToOptional(compiler_type.GetBitSize(&thread));
    if (!bit_size)
      return false;
    bool is_signed;
    if (compiler_type.IsIntegerOrEnumerationType(is_signed))
      ReadIntegerArgument(value->GetScalar(), *bit_size, is_signed, thread,
                          argument_register_ids, current_argument_register,
                          current_stack_argument);
    else if (compiler_type.IsPointerType())
      ReadIntegerArgument(value->GetScalar(), *bit_size, false, thread,
                          argument_register_ids, current_argument_register,
                          current_stack_argument);
  }

  return true;
}

Status ABISysV_sh::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                        lldb::ValueObjectSP &new_value_sp) {
  Status error;
  if (!new_value_sp)
    return Status::FromErrorString("Empty value object for return value.");

  CompilerType compiler_type = new_value_sp->GetCompilerType();
  if (!compiler_type)
    return Status::FromErrorString("Null clang type for return value.");

  Thread *thread = frame_sp->GetThread().get();

  bool is_signed;

  RegisterContext *reg_ctx = thread->GetRegisterContext().get();

  bool set_it_simple = false;
  if (compiler_type.IsIntegerOrEnumerationType(is_signed) ||
      compiler_type.IsPointerType()) {
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName("r0", 0);

    DataExtractor data;
    Status data_error;
    size_t num_bytes = new_value_sp->GetData(data, data_error);
    if (data_error.Fail())
      return Status::FromErrorStringWithFormat(
          "Couldn't convert return value to raw data: %s",
          data_error.AsCString());
    lldb::offset_t offset = 0;
    if (num_bytes <= 4) {
      uint64_t raw_value = data.GetMaxU64(&offset, num_bytes);

      if (reg_ctx->WriteRegisterFromUnsigned(reg_info, raw_value))
        set_it_simple = true;
    } else {
      error = Status::FromErrorString(
          "We don't support returning longer than 32 bit "
          "integer values at present.");
    }
  } else if (compiler_type.IsRealFloatingPointType()) {
    std::optional<uint64_t> bit_width =
        llvm::expectedToOptional(compiler_type.GetBitSize(frame_sp.get()));
    if (!bit_width) {
      error = Status::FromErrorString("can't get type size");
      return error;
    }
    if (*bit_width <= 64) {
      DataExtractor data;
      Status data_error;
      size_t num_bytes = new_value_sp->GetData(data, data_error);
      if (data_error.Fail()) {
        error = Status::FromErrorStringWithFormat(
            "Couldn't convert return value to raw data: %s",
            data_error.AsCString());
        return error;
      }

      unsigned char buffer[8];
      ByteOrder byte_order = data.GetByteOrder();

      data.CopyByteOrderedData(0, num_bytes, buffer, 8, byte_order);
      set_it_simple = true;
    } else {
      error = Status::FromErrorString(
          "We don't support returning float values > 64 bits at present");
    }
  }

  if (!set_it_simple) {
    error = Status::FromErrorString(
        "We only support setting simple integer and float "
        "return types at present.");
  }

  return error;
}

ValueObjectSP ABISysV_sh::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  Value value;

  if (!return_compiler_type)
    return return_valobj_sp;

  value.SetCompilerType(return_compiler_type);

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  const uint32_t type_flags = return_compiler_type.GetTypeInfo();
  if (type_flags & eTypeIsScalar) {
    value.SetValueType(Value::ValueType::Scalar);

    bool success = false;
    if (type_flags & eTypeIsInteger) {
      std::optional<uint64_t> byte_size =
          llvm::expectedToOptional(return_compiler_type.GetByteSize(&thread));
      if (!byte_size)
        return return_valobj_sp;
      uint64_t raw_value = thread.GetRegisterContext()->ReadRegisterAsUnsigned(
          reg_ctx->GetRegisterInfoByName("r0", 0), 0);
      const bool is_signed = (type_flags & eTypeIsSigned) != 0;
      switch (*byte_size) {
      default:
        break;

      case sizeof(uint32_t):
        if (is_signed)
          value.GetScalar() = (int32_t)(raw_value & UINT32_MAX);
        else
          value.GetScalar() = (uint32_t)(raw_value & UINT32_MAX);
        success = true;
        break;

      case sizeof(uint16_t):
        if (is_signed)
          value.GetScalar() = (int16_t)(raw_value & UINT16_MAX);
        else
          value.GetScalar() = (uint16_t)(raw_value & UINT16_MAX);
        success = true;
        break;

      case sizeof(uint8_t):
        if (is_signed)
          value.GetScalar() = (int8_t)(raw_value & UINT8_MAX);
        else
          value.GetScalar() = (uint8_t)(raw_value & UINT8_MAX);
        success = true;
        break;
      }
    } else if (type_flags & eTypeIsFloat) {
      std::optional<uint64_t> byte_size =
          llvm::expectedToOptional(return_compiler_type.GetByteSize(&thread));
      if (byte_size && *byte_size <= sizeof(double)) {
        const RegisterInfo *fr0_info = reg_ctx->GetRegisterInfoByName("fr0", 0);
        RegisterValue fr0_value;
        if (reg_ctx->ReadRegister(fr0_info, fr0_value)) {
          DataExtractor data;
          if (fr0_value.GetData(data)) {
            lldb::offset_t offset = 0;
            if (*byte_size == sizeof(float)) {
              value.GetScalar() = (float)data.GetFloat(&offset);
              success = true;
            } else if (*byte_size == sizeof(double)) {
              value.GetScalar() = (double)data.GetDouble(&offset);
              success = true;
            }
          }
        }
      }
    }

    if (success)
      return_valobj_sp = ValueObjectConstResult::Create(
          thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  } else if (type_flags & eTypeIsPointer) {
    unsigned r0_id =
        reg_ctx->GetRegisterInfoByName("r0", 0)->kinds[eRegisterKindLLDB];
    value.GetScalar() =
        (uint64_t)thread.GetRegisterContext()->ReadRegisterAsUnsigned(r0_id, 0);
    value.SetValueType(Value::ValueType::Scalar);
    return_valobj_sp = ValueObjectConstResult::Create(
        thread.GetStackFrameAtIndex(0).get(), value, ConstString(""));
  }

  return return_valobj_sp;
}

ValueObjectSP
ABISysV_sh::GetReturnValueObjectImpl(Thread &thread,
                                     CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;

  if (!return_compiler_type)
    return return_valobj_sp;

  ExecutionContext exe_ctx(thread.shared_from_this());
  return_valobj_sp = GetReturnValueObjectSimple(thread, return_compiler_type);
  if (return_valobj_sp)
    return return_valobj_sp;

  RegisterContextSP reg_ctx_sp = thread.GetRegisterContext();
  if (!reg_ctx_sp)
    return return_valobj_sp;

  return return_valobj_sp; // We do not currently handle large structs in
                           // registers for SH.
}

bool ABISysV_sh::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_sh::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (reg_info) {
    const char *name = reg_info->name;
    if (name[0] == 'r') {
      switch (name[1]) {
      case '8':
      case '9':
        return true;
      case '1':
        if (name[2] == '0' || name[2] == '1' || name[2] == '2' ||
            name[2] == '3' || name[2] == '4')
          return true;
        break;
      }
    } else if (name[0] == 'f' && name[1] == 'r') {
      if (name[2] == '1') {
        if (name[3] == '2' || name[3] == '3' || name[3] == '4' ||
            name[3] == '5')
          return true;
      }
    } else if (name[0] == 'p' && name[1] == 'r') {
      return true; // pr is callee saved
    }
  }
  return false;
}

UnwindPlanSP ABISysV_sh::CreateFunctionEntryUnwindPlan() {
  UnwindPlanSP unwind_plan_sp(new UnwindPlan(eRegisterKindGeneric));

  uint32_t cfa_reg_num = LLDB_REGNUM_GENERIC_SP;
  uint32_t pc_reg_num = LLDB_REGNUM_GENERIC_PC;
  uint32_t ra_reg_num = LLDB_REGNUM_GENERIC_RA;

  UnwindPlan::Row row;

  // CFA is sp
  row.GetCFAValue().SetIsRegisterPlusOffset(cfa_reg_num, 0);

  // PC is strictly PR (Link Register) on entry
  row.SetRegisterLocationToRegister(pc_reg_num, ra_reg_num, true);

  unwind_plan_sp->AppendRow(row);
  unwind_plan_sp->SetSourceName("sh at-func-entry default");
  unwind_plan_sp->SetSourcedFromCompiler(eLazyBoolNo);

  return unwind_plan_sp;
}

UnwindPlanSP ABISysV_sh::CreateDefaultUnwindPlan() {
  UnwindPlanSP unwind_plan_sp(new UnwindPlan(eRegisterKindGeneric));

  uint32_t cfa_reg_num = LLDB_REGNUM_GENERIC_SP;
  uint32_t pc_reg_num = LLDB_REGNUM_GENERIC_PC;
  uint32_t fp_reg_num = LLDB_REGNUM_GENERIC_FP;

  UnwindPlan::Row row;

  // If defaulting, CFA is usually FP
  row.GetCFAValue().SetIsRegisterPlusOffset(fp_reg_num, 0);

  // PC is usually stored at CFA+4 based on typical generic layouts, but DWARF
  // normally drives SH.
  row.SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, 4, true);

  // SP is CFA
  row.SetRegisterLocationToIsCFAPlusOffset(cfa_reg_num, 0, true);

  unwind_plan_sp->AppendRow(row);
  unwind_plan_sp->SetSourceName("sh default unwind plan");
  unwind_plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);

  return unwind_plan_sp;
}

void ABISysV_sh::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "System V ABI for SH targets", CreateInstance);
}

void ABISysV_sh::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

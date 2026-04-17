//===----- lib/sh/fp_mode.c - Floating-point mode utilities --------*- C
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../fp_mode.h"
#include <stdint.h>

// SH FPSCR (Floating-point status/control register)
// RM (Rounding Mode): bit 0
// 0: Round to Nearest
// 1: Round to Zero
#define SH_TONEAREST 0x0
#define SH_TOWARDZERO 0x1
#define SH_RMODE_MASK 0x1

// Cause bits
#define SH_INEXACT 0x00000004 // bit 2 is Cause Inexact

CRT_FE_ROUND_MODE __fe_getround(void) {
#if defined(__sh__)
  uint32_t fpscr;
  __asm__ __volatile__("sts fpscr, %0" : "=r"(fpscr));
  fpscr = fpscr & SH_RMODE_MASK;
  switch (fpscr) {
  case SH_TOWARDZERO:
    return CRT_FE_TOWARDZERO;
  case SH_TONEAREST:
  default:
    return CRT_FE_TONEAREST;
  }
#else
  return CRT_FE_TONEAREST;
#endif
}

int __fe_raise_inexact(void) {
#if defined(__sh__)
  uint32_t fpscr;
  __asm__ __volatile__("sts fpscr, %0" : "=r"(fpscr));
  __asm__ __volatile__("lds %0, fpscr" : : "r"(fpscr | SH_INEXACT));
  return 0;
#else
  return 0;
#endif
}

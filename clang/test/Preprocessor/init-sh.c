// RUN: %clang_cc1 -E -dM -ffreestanding -triple=sh-unknown-linux-gnu < /dev/null | FileCheck -match-full-lines -check-prefix SH %s
//
// SH:#define __sh4__ 1
// SH:#define __sh__ 1

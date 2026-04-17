// RUN: %clang --target=sh-linux-gnu                 -E -dM %s -o - | FileCheck --check-prefix=SH1        %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh1        -E -dM %s -o - | FileCheck --check-prefix=SH1        %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh2        -E -dM %s -o - | FileCheck --check-prefix=SH2        %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh2e       -E -dM %s -o - | FileCheck --check-prefix=SH2E       %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh2a       -E -dM %s -o - | FileCheck --check-prefix=SH2A       %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh2a-fpu   -E -dM %s -o - | FileCheck --check-prefix=SH2A_FPU   %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh3        -E -dM %s -o - | FileCheck --check-prefix=SH3        %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh3e       -E -dM %s -o - | FileCheck --check-prefix=SH3E       %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh4-nofpu  -E -dM %s -o - | FileCheck --check-prefix=SH4_NOFPU  %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh4        -E -dM %s -o - | FileCheck --check-prefix=SH4        %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh4a-nofpu -E -dM %s -o - | FileCheck --check-prefix=SH4A_NOFPU %s
// RUN: %clang --target=sh-linux-gnu -mcpu=sh4a       -E -dM %s -o - | FileCheck --check-prefix=SH4A       %s

// Predefined macros match GCC's SH backend verbatim (gcc/config/sh/sh-c.cc).
// -dM emits macros in sorted order, so CHECK-DAG is used for assertions.

// SH1-DAG:      #define __SH1__ 1
// SH1-DAG:      #define __sh1__ 1
// SH1-NOT:      __SH_FPU_ANY__
// SH1-NOT:      __SH2
// SH1-NOT:      __SH3
// SH1-NOT:      __SH4

// SH2-DAG:      #define __SH2__ 1
// SH2-DAG:      #define __sh2__ 1
// SH2-NOT:      __SH_FPU_ANY__
// SH2-NOT:      __SH2E__
// SH2-NOT:      __SH2A__

// SH2E-DAG:     #define __SH2E__ 1
// SH2E-DAG:     #define __SH2__ 1
// SH2E-DAG:     #define __SH_FPU_ANY__ 1
// SH2E-NOT:     __SH_FPU_DOUBLE__
// SH2E-NOT:     __sh2e__
// SH2E-NOT:     __SH2A__

// SH2A-DAG:     #define __SH2A__ 1
// SH2A-DAG:     #define __SH2A_NOFPU__ 1
// SH2A-NOT:     __SH_FPU_ANY__
// SH2A-NOT:     __sh2a__

// SH2A_FPU-DAG: #define __SH2A__ 1
// SH2A_FPU-DAG: #define __SH2A_DOUBLE__ 1
// SH2A_FPU-DAG: #define __SH_FPU_ANY__ 1
// SH2A_FPU-DAG: #define __SH_FPU_DOUBLE__ 1
// SH2A_FPU-NOT: __SH2A_FPU__
// SH2A_FPU-NOT: __SH2A_NOFPU__

// SH3-DAG:      #define __SH3__ 1
// SH3-DAG:      #define __sh3__ 1
// SH3-NOT:      __SH_FPU_ANY__
// SH3-NOT:      __SH3E__
// SH3-NOT:      __SH4__

// SH3E-DAG:     #define __SH3E__ 1
// SH3E-DAG:     #define __SH3__ 1
// SH3E-DAG:     #define __SH_FPU_ANY__ 1
// SH3E-NOT:     __SH_FPU_DOUBLE__
// SH3E-NOT:     __sh3e__

// SH4_NOFPU-DAG:     #define __SH4__ 1
// SH4_NOFPU-DAG:     #define __SH4_NOFPU__ 1
// SH4_NOFPU-NOT:     __SH_FPU_ANY__
// SH4_NOFPU-NOT:     __SH4A__

// SH4-DAG:      #define __SH4__ 1
// SH4-DAG:      #define __SH_FPU_ANY__ 1
// SH4-DAG:      #define __SH_FPU_DOUBLE__ 1
// SH4-NOT:      __SH4A__
// SH4-NOT:      __sh4__
// SH4-NOT:      __SH4_NOFPU__

// SH4A_NOFPU-DAG:    #define __SH4A__ 1
// SH4A_NOFPU-DAG:    #define __SH4__ 1
// SH4A_NOFPU-DAG:    #define __SH4_NOFPU__ 1
// SH4A_NOFPU-NOT:    __SH_FPU_ANY__
// SH4A_NOFPU-NOT:    __SH4A_NOFPU__

// SH4A-DAG:     #define __SH4A__ 1
// SH4A-DAG:     #define __SH4__ 1
// SH4A-DAG:     #define __SH_FPU_ANY__ 1
// SH4A-DAG:     #define __SH_FPU_DOUBLE__ 1
// SH4A-NOT:     __sh4a__
// SH4A-NOT:     __SH4_NOFPU__

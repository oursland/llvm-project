// RUN: %clang -### --target=sh-unknown-linux-gnu %s 2>&1 | FileCheck --check-prefix=TRIPLE %s
// RUN: %clang -### --target=sh-unknown-linux-gnu -fpic %s 2>&1 | FileCheck --check-prefix=PIC %s
// RUN: %clang -### --target=sh-unknown-linux-gnu -static %s 2>&1 | FileCheck --check-prefix=STATIC %s
// RUN: %clang -### --target=sh-unknown-linux-gnu -shared %s 2>&1 | FileCheck --check-prefix=SHARED %s
// RUN: %clang -### --target=sh-unknown-linux-gnu -O2 %s 2>&1 | FileCheck --check-prefix=FP-OMIT %s
// RUN: %clang -### --target=sh-unknown-linux-gnu -O0 %s 2>&1 | FileCheck --check-prefix=FP-KEEP %s

// TRIPLE: "-triple" "sh-unknown-linux-gnu"

// PIC: "-mrelocation-model" "pic"

// STATIC: "-static"

// SHARED: "-shared"

// At -O2, frame pointer should be omitted.
// FP-OMIT: "-mframe-pointer=none"

// At -O0, frame pointer should be kept (frame-pointer=all).
// FP-KEEP: "-mframe-pointer=all"

// RUN: %clang_cc1 -triple sh-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// Use builtins directly; stdarg.h is not available in -cc1 mode.
typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

// Test variadic function declaration - first arg in register, va_start for rest.
// CHECK-LABEL: define{{.*}} i32 @sum_varargs(i32 inreg noundef %count, ...)
int sum_varargs(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(ap, int);
    }
    va_end(ap);
    return sum;
}

// Test calling a variadic function.
// CHECK-LABEL: define{{.*}} i32 @call_varargs()
int call_varargs(void) {
    // CHECK: call i32 (i32, ...) @sum_varargs(i32 inreg noundef 3, i32 inreg noundef 10, i32 inreg noundef 20, i32 inreg noundef 30)
    return sum_varargs(3, 10, 20, 30);
}

// Test printf-like variadic.
int printf(const char *fmt, ...);

// CHECK-LABEL: define{{.*}} void @test_printf()
void test_printf(void) {
    // CHECK: call i32 (ptr, ...) @printf
    printf("hello %d\n", 42);
}

// Test va_copy.
// CHECK-LABEL: define{{.*}} i32 @test_va_copy(i32 inreg noundef %n, ...)
int test_va_copy(int n, ...) {
    va_list ap, ap2;
    va_start(ap, n);
    va_copy(ap2, ap);
    int first = va_arg(ap, int);
    int also_first = va_arg(ap2, int);
    va_end(ap);
    va_end(ap2);
    return first + also_first;
}

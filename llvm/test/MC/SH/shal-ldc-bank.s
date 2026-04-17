! RUN: llvm-mc -triple=sh-unknown-linux-gnu -show-encoding %s | FileCheck %s

! SHAL — shift arithmetic left (0100 nnnn 0010 0000)
! CHECK: shal r0    ! encoding: [0x20,0x40]
shal r0
! CHECK: shal r4    ! encoding: [0x20,0x44]
shal r4
! CHECK: shal r15   ! encoding: [0x20,0x4f]
shal r15

! LDC Rm,Rn_BANK — load to banked register (0100 mmmm 1nnn 1110)
! CHECK: ldc r0, r0_bank   ! encoding: [0x8e,0x40]
ldc r0, r0_bank
! CHECK: ldc r5, r1_bank   ! encoding: [0x9e,0x45]
ldc r5, r1_bank
! CHECK: ldc r3, r2_bank   ! encoding: [0xae,0x43]
ldc r3, r2_bank
! CHECK: ldc r1, r3_bank   ! encoding: [0xbe,0x41]
ldc r1, r3_bank
! CHECK: ldc r2, r4_bank   ! encoding: [0xce,0x42]
ldc r2, r4_bank
! CHECK: ldc r7, r5_bank   ! encoding: [0xde,0x47]
ldc r7, r5_bank
! CHECK: ldc r6, r6_bank   ! encoding: [0xee,0x46]
ldc r6, r6_bank
! CHECK: ldc r4, r7_bank   ! encoding: [0xfe,0x44]
ldc r4, r7_bank

! LDC.L @Rm+,Rn_BANK — load to banked register, post-increment (0100 mmmm 1nnn 0111)
! CHECK: ldc.l @r0+, r0_bank   ! encoding: [0x87,0x40]
ldc.l @r0+, r0_bank
! CHECK: ldc.l @r5+, r1_bank   ! encoding: [0x97,0x45]
ldc.l @r5+, r1_bank
! CHECK: ldc.l @r3+, r2_bank   ! encoding: [0xa7,0x43]
ldc.l @r3+, r2_bank
! CHECK: ldc.l @r1+, r3_bank   ! encoding: [0xb7,0x41]
ldc.l @r1+, r3_bank
! CHECK: ldc.l @r2+, r4_bank   ! encoding: [0xc7,0x42]
ldc.l @r2+, r4_bank
! CHECK: ldc.l @r7+, r5_bank   ! encoding: [0xd7,0x47]
ldc.l @r7+, r5_bank
! CHECK: ldc.l @r6+, r6_bank   ! encoding: [0xe7,0x46]
ldc.l @r6+, r6_bank
! CHECK: ldc.l @r4+, r7_bank   ! encoding: [0xf7,0x44]
ldc.l @r4+, r7_bank

# Copyright(C) 2019 Intel Corporation.All rights reserved.
# SPDX - License - Identifier : Apache - 2.0 WITH LLVM - exception

set (LIBC_WALI_DIR ${CMAKE_CURRENT_LIST_DIR})
set (ARCH ${CMAKE_SYSTEM_PROCESSOR})

add_definitions (-DWASM_ENABLE_LIBC_WALI=1)

include_directories(${LIBC_WALI_DIR}/wali_arch/${ARCH})

# Compile flags
add_compile_definitions(WALI_ENABLE_SYSCALL_PROFILE=0)
add_compile_definitions(WALI_ENABLE_NATIVE_SYSCALL_PROFILE=0)

# Setup files to compile
file (GLOB_RECURSE wali_arch_sources 
    ${LIBC_WALI_DIR}/wali_arch/${ARCH}/*.s 
    ${LIBC_WALI_DIR}/wali_arch/*.c
    )
set (WALI_SOURCES ${LIBC_WALI_DIR}/wali.c ${wali_arch_sources})

set (LIBC_WALI_SOURCE ${WALI_SOURCES})

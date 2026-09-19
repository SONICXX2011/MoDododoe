cmake_minimum_required(VERSION 3.22.1)
project(mymod C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ═══════════════════════════════════════════════════════
# ShadowHook
# ═══════════════════════════════════════════════════════
set(SH_COMMON_SRC
    shadowhook/shadowhook.c
    shadowhook/sh_elf.c
    shadowhook/sh_enter.c
    shadowhook/sh_hub.c
    shadowhook/sh_island.c
    shadowhook/sh_jni.c
    shadowhook/sh_linker.c
    shadowhook/sh_recorder.c
    shadowhook/sh_safe.c
    shadowhook/sh_switch.c
    shadowhook/sh_task.c
    shadowhook/sh_xdl.c
    shadowhook/common/bytesig.c
    shadowhook/common/sh_errno.c
    shadowhook/common/sh_log.c
    shadowhook/common/sh_ref.c
    shadowhook/common/sh_trampo.c
    shadowhook/common/sh_util.c
    shadowhook/third_party/xdl/xdl.c
    shadowhook/third_party/xdl/xdl_iterate.c
    shadowhook/third_party/xdl/xdl_linker.c
    shadowhook/third_party/xdl/xdl_lzma.c
    shadowhook/third_party/xdl/xdl_util.c
)

if("${ANDROID_ABI}" STREQUAL "arm64-v8a")
    set(SH_ARCH_SRC
        shadowhook/arch/arm64/sh_a64.c
        shadowhook/arch/arm64/sh_glue.S
        shadowhook/arch/arm64/sh_inst.c
    )
    set(SH_ARCH_INC shadowhook/arch/arm64)
elseif("${ANDROID_ABI}" STREQUAL "armeabi-v7a")
    set(SH_ARCH_SRC
        shadowhook/arch/arm/sh_a32.c
        shadowhook/arch/arm/sh_glue.S
        shadowhook/arch/arm/sh_inst.c
        shadowhook/arch/arm/sh_t16.c
        shadowhook/arch/arm/sh_t32.c
        shadowhook/arch/arm/sh_txx.c
    )
    set(SH_ARCH_INC shadowhook/arch/arm)
endif()

add_library(shadowhook STATIC ${SH_COMMON_SRC} ${SH_ARCH_SRC})

target_include_directories(shadowhook PUBLIC
    shadowhook
    shadowhook/include
    ${SH_ARCH_INC}
    shadowhook/common
    shadowhook/third_party/xdl
    shadowhook/third_party/bsd
    shadowhook/third_party/lss
)

target_compile_options(shadowhook PRIVATE
    -O2
    -fvisibility=hidden
    -fno-stack-protector
)

# ═══════════════════════════════════════════════════════
# BNM
# ═══════════════════════════════════════════════════════
set(BNM_SRC
    BNM/src/Class.cpp
    BNM/src/ClassesManagement.cpp
    BNM/src/Coroutine.cpp
    BNM/src/Defaults.cpp
    BNM/src/Delegates.cpp
    BNM/src/EventBase.cpp
    BNM/src/Exceptions.cpp
    BNM/src/FieldBase.cpp
    BNM/src/Hooks.cpp
    BNM/src/Image.cpp
    BNM/src/Internals.cpp
    BNM/src/Loading.cpp
    BNM/src/MethodBase.cpp
    BNM/src/MonoStructures.cpp
    BNM/src/PropertyBase.cpp
    BNM/src/UnityStructures.cpp
    BNM/src/Utils.cpp
)

# ═══════════════════════════════════════════════════════
# Mod sources
# ═══════════════════════════════════════════════════════
set(MOD_SRC
    main.cpp
    JavaBridge.cpp
    ModCore.cpp
)

add_library(mymod SHARED
    ${MOD_SRC}
    ${BNM_SRC}
)

target_include_directories(mymod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    BNM/include
    BNM/external/include
    BNM/external
    BNM/external/utf8
    BNM/src/private
    shadowhook/include
)

target_compile_options(mymod PRIVATE
    -fexceptions
    -frtti
    -O2
    -fvisibility=hidden
)

set_source_files_properties(${BNM_SRC} PROPERTIES
    COMPILE_OPTIONS "-include;cmath"
)

target_link_libraries(mymod
    shadowhook
    log
    android
    dl
)
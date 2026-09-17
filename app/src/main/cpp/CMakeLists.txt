cmake_minimum_required(VERSION 3.22.1)
project(mymod CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ ۴ سطح بالا: cpp → main → src → app → ROOT
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

message(STATUS "ROOT_DIR = ${ROOT_DIR}")

# ═══ ShadowHook ═══
add_subdirectory(
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp
    ${CMAKE_BINARY_DIR}/shadowhook
)

# ═══ BNM ═══
add_subdirectory(
    ${ROOT_DIR}/BNM
    ${CMAKE_BINARY_DIR}/BNM
)
get_property(BNM_INC TARGET BNM PROPERTY BNM_INCLUDE_DIRECTORIES)
include_directories(${BNM_INC})

# ═══ KittyMemory ═══
add_subdirectory(
    ${ROOT_DIR}/kittymemory/KittyMemory
    ${CMAKE_BINARY_DIR}/kittymemory
)

# ═══ ImGui ═══
set(IMGUI_DIR ${ROOT_DIR}/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ═══ Main module ═══
add_library(mymod SHARED main.cpp)

target_include_directories(mymod PRIVATE
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp/include
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp/arch/arm64
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp/common
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp/third_party/xdl
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp/third_party/bsd
    ${ROOT_DIR}/shadowhook/shadowhook/src/main/cpp/third_party/lss
    ${ROOT_DIR}/BNM/include
    ${ROOT_DIR}/BNM/external/include
    ${ROOT_DIR}/BNM/external
    ${ROOT_DIR}/BNM/external/utf8
    ${ROOT_DIR}/BNM/src/private
    ${ROOT_DIR}/kittymemory/KittyMemory
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(mymod
    BNM
    shadowhook
    KittyMemory
    imgui
    log
    android
    EGL
    GLESv3
)

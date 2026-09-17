cmake_minimum_required(VERSION 3.21)
project(KittyMemory)

set(CMAKE_CXX_STANDARD 17)

add_library(KittyMemory STATIC
    KittyMemory.cpp
    KittyUtils.cpp
    KittyScanner.cpp
    KittyPtrValidator.cpp
    KittyIOFile.cpp
    MemoryBackup.cpp
    MemoryPatch.cpp
)

target_include_directories(KittyMemory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(KittyMemory PUBLIC log)

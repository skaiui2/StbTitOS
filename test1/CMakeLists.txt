cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32f1)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})


file(GLOB LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/lib/source/*.c")
file(GLOB MG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/mg/source/*.c")
file(GLOB FS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/fs/source/*.c")
file(GLOB COMPILER_BACKEND_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/backend/source/*.c")
file(GLOB COMPILER_FRONTEND_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/frontend/source/*.c")
file(GLOB COMPILER_IR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/ir/source/*.c")
file(GLOB VM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/vm/bpf/source/*.c")
file(GLOB RTOS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/RTOS/source/*.c")
file(GLOB PORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../arch/stm32f103c8t6/armcm3/*.c")
file(GLOB SHELL_PORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../arch/stm32f103c8t6/shell_port/*.c")
file(GLOB FS_PORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../arch/stm32f103c8t6/fs_port/*.c")
file(GLOB CCNET_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccnet/source/*.c")
file(GLOB CCRPC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccrpc/source/*.c")
file(GLOB SCP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/scp/source/*.c")
file(GLOB SHELL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/shell/source/*.c")
file(GLOB VIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/vim/source/*.c")
file(GLOB CLUSTER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/cluster/source/*.c")
file(GLOB VFS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lttit/vfs/source/*.c")

add_executable(${CMAKE_PROJECT_NAME}
        ${LIB_SOURCES}
        ${MG_SOURCES}
        ${FS_SOURCES}
        ${COMPILER_BACKEND_SOURCES}
        ${COMPILER_FRONTEND_SOURCES}
        ${COMPILER_IR_SOURCES}
        ${VM_SOURCES}
        ${RTOS_SOURCES}
        ${PORT_SOURCES}
        ${CCNET_SOURCES}
        ${SCP_SOURCES}
        ${SHELL_SOURCES}
        ${VIM_SOURCES}
        ${CCRPC_SOURCES}
        ${SHELL_PORT_SOURCES}
        ${FS_PORT_SOURCES}
        ${CLUSTER_SOURCES}
        ${VFS_SOURCES}
        Core/Inc/lttit_config.h
)


include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/lib/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/mg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/fs/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/frontend/include/
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/backend/include/
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/ir/include/
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/vm/bpf/include/
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/RTOS/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../arch/stm32f103c8t6/armcm3
        ${CMAKE_CURRENT_SOURCE_DIR}/../arch/stm32f103c8t6/shell_port
        ${CMAKE_CURRENT_SOURCE_DIR}/../arch/stm32f103c8t6/fs_port
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccnet/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/scp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/shell/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/vim/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccrpc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/cluster/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/vfs/include
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

cmake_minimum_required(VERSION 3.13)
set(PICO_MAKE_LINK_MAP 1)
set(PICO_SDK_PATH "/home/skaiuijing/Downloads/pico-sdk")
set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)
project(my_blink C CXX ASM)
pico_sdk_init()
add_compile_options(-fno-lto)
add_link_options(-fno-lto)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${PROJECT_NAME}.final.map")

file(GLOB_RECURSE LTTIT_CCBPF_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/backend/source/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/frontend/source/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/ir/source/*.c
)
file(GLOB_RECURSE LTTIT_VM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/vm/bpf/source/*.c
)
file(GLOB_RECURSE LTTIT_WORLD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/world/source/*.c
)
file(GLOB_RECURSE LTTIT_CCNET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccnet/source/*.c
)
file(GLOB_RECURSE LTTIT_SCP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/scp/source/*.c
)
file(GLOB_RECURSE LTTIT_CCRPC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccrpc/source/*.c
)
file(GLOB_RECURSE LTTIT_SHELL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/shell/source/*.c
)
file(GLOB_RECURSE LTTIT_VIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/vim/source/*.c
)
file(GLOB_RECURSE LTTIT_FS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/fs/source/*.c
)
file(GLOB_RECURSE LTTIT_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/lib/source/*.c
)
file(GLOB_RECURSE LTTIT_MG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/mg/source/*.c
)
file(GLOB_RECURSE LTTIT_RTOS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/RTOS/source/*.c
)
file(GLOB_RECURSE LTTIT_TCPIP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/TcpIp/source/*.c
)
file(GLOB_RECURSE LTTIT_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../arch/pico2w/armcm33/*.c
)
file(GLOB_RECURSE LTTIT_FS_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../arch/pico2w/fs_port/*.c
)
file(GLOB_RECURSE LTTIT_SHELL_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../arch/pico2w/shell_port/*.c
)

add_executable(my_blink
    blink.c
    ${LTTIT_CCBPF_SOURCES}
    ${LTTIT_WORLD_SOURCES}
    ${LTTIT_VM_SOURCES}
    ${LTTIT_CCNET_SOURCES}
    ${LTTIT_CCRPC_SOURCES}
    ${LTTIT_SCP_SOURCES}
    ${LTTIT_SHELL_SOURCES}
    ${LTTIT_FS_SOURCES}
    ${LTTIT_VIM_SOURCES}
    ${LTTIT_LIB_SOURCES}
    ${LTTIT_MG_SOURCES}
    ${LTTIT_RTOS_SOURCES}
    ${LTTIT_TCPIP_SOURCES}
    ${LTTIT_PORT_SOURCES}
    ${LTTIT_FS_PORT_SOURCES}
    ${LTTIT_SHELL_PORT_SOURCES}
)

target_include_directories(my_blink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../arch/pico2w/armcm33
    ${CMAKE_CURRENT_SOURCE_DIR}/../arch/pico2w/fs_port
    ${CMAKE_CURRENT_SOURCE_DIR}/../arch/pico2w/shell_port
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/mg/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/RTOS/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/world/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/fs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/shell/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/vim/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccnet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/scp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/CSC/ccrpc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/frontend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/compiler/ir/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/ccBPF/vm/bpf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lttit/TcpIp/include
)

target_link_libraries(my_blink
    pico_stdlib
    pico_cyw43_arch_none
    pico_multicore
)

pico_enable_stdio_uart(my_blink 1)
pico_enable_stdio_usb(my_blink 0)
pico_add_extra_outputs(my_blink)

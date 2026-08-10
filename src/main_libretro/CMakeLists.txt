# FM Towns (Tsugaru) libretro core
#
# 该 target 产出一个独立的动态库（towns_libretro.dll / .so / .dylib），
# 不依赖 SDL / yssimplesound，所有视频/音频/输入都通过 Outside_World 的
# libretro 专用实现完成。
#
# 链接策略：towns 库已用 PUBLIC 方式把 cpu / vmbase / device / 各 Towns 子系统
# 等全部传递链接进来；但有几个 leaf 库是"按可执行文件决定"或 PRIVATE 依赖，
# 不会经 towns 传播，必须在这里显式补：
#   - outside_world : LibRetro_World 的基类所在库
#   - yssimplesound : townssound 仅用其头文件，真正的 SoundData 实现(含 nownd 无声后端)在此
#   - filesys       : townstgdrv 以 PRIVATE 方式依赖，FileSys::Find* 实现在此
#   - network / yssocket_export : towns 的 PRIVATE 依赖，引用 WinSock 符号，
#                                  不传播到 core，须显式补；且 -lws2_32 必须排在它们之后
#   - ws2_32        : WinSock 系统导入库（__imp_WSAStartup / socket / ...），
#                                  MinGW 下不自动传播，用 target_link_options 钉在链接行最末

add_library(towns_libretro SHARED libretro.cpp)

target_include_directories(towns_libretro PRIVATE
    ${CMAKE_SOURCE_DIR}            # discimg/discimg.h, towns/towns.h ...
    ${CMAKE_SOURCE_DIR}/towns      # towns.h, render/render.h ...
    ${CMAKE_SOURCE_DIR}/discimg    # discimg.h
    ${CMAKE_CURRENT_SOURCE_DIR})   # libretro.h, keymap.h

# libretro 是单线程模型：启用 SCSI IO 同步模式（不创建后台线程/锁），
# 消除 TOWNSEMU 后台 IO 线程在 RA 环境下导致的 winpthread 死锁。
target_compile_definitions(towns_libretro PRIVATE TOWNS_LIBRETRO_SYNC_SCSI=1)

if(WIN32)
    # ---- MinGW 链接策略（自包含：静态 winpthread）----
    # 目标：产出一个不依赖任何第三方 DLL 的单文件 core（导入表只留系统 DLL）。
    #   - -static-libgcc / -static-libstdc++：静态链入 C/C++ 运行时（纯计算库，安全）。
    #   - -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic：把 winpthread 也静态链入，
    #     这样 std::thread / std::mutex 不再依赖外部的 libwinpthread-1.dll。
    # 历史警示：早期在 SCSI/CD-ROM 仍用【异步后台线程】时，静态 winpthread 曾导致
    #   RetroArch 加载卡死（静态 pthread 在复杂多线程宿主下破坏 std::thread 行为）。
    #   但本 core 已用 TOWNS_LIBRETRO_SYNC_SCSI 将 SCSI/CD-ROM 改为同步单线程，
    #   且不启用网络线程，运行时不再真正创建线程，因此静态链入是安全的。
    target_link_libraries(towns_libretro
        towns osinit outside_world yssimplesound filesys network yssocket_export ws2_32)
    target_link_options(towns_libretro PRIVATE
        "-Wl,--as-needed"
        "-static-libgcc" "-static-libstdc++"
        "-static")
else()
    # 非 Windows：towns 已 PUBLIC 传递 cpu/vmbase/device 等；按需要补 leaf 库。
    target_link_libraries(towns_libretro
        towns osinit outside_world yssimplesound filesys)
endif()

set_target_properties(towns_libretro PROPERTIES
    PREFIX ""
    OUTPUT_NAME "towns_libretro")

if(UNIX)
    # 现代 glibc 中 pthread 已并入 libc；老版本仍需显式链接
    target_link_libraries(towns_libretro pthread)
    target_compile_options(towns_libretro PRIVATE -Wno-unused-variable -Wno-unused-parameter)
endif()

if(WIN32)
    # Windows DLL 默认不导出符号，需要 .def 文件列出所有 retro_* 导出项
    target_sources(towns_libretro PRIVATE libretro.def)
endif()

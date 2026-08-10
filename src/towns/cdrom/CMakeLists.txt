add_library(townscdrom cdrom.h cdrom.cpp)
target_link_libraries(townscdrom device cpputil townsdef discimg towns outside_world)
target_include_directories(townscdrom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# libretro core 单线程模型：禁用 CD 波形读取后台线程，避免 winpthread 死锁
if(BUILD_LIBRETRO)
target_compile_definitions(townscdrom PRIVATE TOWNS_LIBRETRO_SYNC_SCSI=1)
endif()

# kernel/drivers 通用驱动模块

# 查找当前目录的所有.c文件
file(GLOB DRIVERS_SOURCES "*.c")

file(GLOB CHILD_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

foreach(dir ${CHILD_DIRS})
  set(dir_full ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
  if(IS_DIRECTORY ${dir_full})
    if(EXISTS ${dir_full}/CMakeLists.txt)
      add_subdirectory(${dir})
    endif()
  endif()
endforeach()

# 创建静态库
add_library(kernel_drivers STATIC ${DRIVERS_SOURCES})

# 添加私有包含路径
target_include_directories(kernel_drivers PRIVATE .)

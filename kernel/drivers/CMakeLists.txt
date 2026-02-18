# kernel/drivers 通用驱动模块

# 创建空的主驱动库
add_library(kernel_drivers STATIC)

# 添加当前目录源文件
file(GLOB ROOT_SOURCES "*.c")
if(ROOT_SOURCES)
    target_sources(kernel_drivers PRIVATE ${ROOT_SOURCES})
endif()

# 遍历子目录，添加子模块
file(GLOB CHILD_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(dir ${CHILD_DIRS})
  set(dir_full ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
  if(IS_DIRECTORY ${dir_full})
    if(EXISTS ${dir_full}/CMakeLists.txt)
      add_subdirectory(${dir})
    endif()
  endif()
endforeach()
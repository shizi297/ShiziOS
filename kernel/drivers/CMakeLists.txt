# kernel/drivers 通用驱动模块

# 创建主驱动静态库
add_library(kernel_drivers STATIC)

# 添加当前目录源文件
file(GLOB ROOT_SOURCES "*.c")
if(ROOT_SOURCES)
    target_sources(kernel_drivers PRIVATE ${ROOT_SOURCES})
endif()

# 初始化全局收集变量
set(ALL_DRIVER_OBJECTS "" CACHE INTERNAL "Collected driver object files")

# 递归添加所有子目录
function(add_all_subdirs dir)
    file(GLOB_RECURSE all_cmake RELATIVE ${dir} ${dir}/CMakeLists.txt)
    foreach(cmake_file ${all_cmake})
        get_filename_component(subdir ${cmake_file} PATH)
        if(subdir AND EXISTS ${dir}/${subdir}/CMakeLists.txt)
            add_subdirectory(${dir}/${subdir})
        endif()
    endforeach()
endfunction()

add_all_subdirs(${CMAKE_CURRENT_SOURCE_DIR})

# 将所有收集到的对象文件添加到 kernel_drivers
if(ALL_DRIVER_OBJECTS)
    target_sources(kernel_drivers PRIVATE ${ALL_DRIVER_OBJECTS})
endif()
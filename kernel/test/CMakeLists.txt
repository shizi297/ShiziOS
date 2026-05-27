# kernel/test 测试模块
file(GLOB_RECURSE TEST_SOURCES "*.c")
add_library(kernel_test STATIC ${TEST_SOURCES})

set(VNA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(VNA_SOURCE_DIR "${VNA_ROOT_DIR}/source")

add_subdirectory(3rdparty)
add_subdirectory(source/runtime)
add_subdirectory(source/adapter)

if(BUILD_TESTING)
    # 只在合同测试组合中把 Runtime/Store 不可达防御分支变成可注入故障；
    # 关闭测试的产品 Runtime 不包含对应字段、friend 或分支。
    target_compile_definitions(VnaRuntime PUBLIC
        VNA_ENABLE_RUNTIME_CONTRACT_TEST_HOOKS=1
        VNA_ENABLE_STORE_CONTRACT_TEST_HOOKS=1
    )
    add_subdirectory(source/test)
endif()

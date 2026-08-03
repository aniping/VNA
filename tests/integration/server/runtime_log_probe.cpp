#include "runtime_log.hpp"

#include <cstdlib>
#include <vna/compat/filesystem.hpp>

#include <spdlog/spdlog.h>

template <typename Character>
int run(int argc, Character** argv) {
    if (argc != 2) {
        return EXIT_FAILURE;
    }
    vna::server::initializeRuntimeLog(
        vna::compat::filesystem::path{argv[1]});
    const auto logger = spdlog::get("vna");
    if (!logger) {
        return EXIT_FAILURE;
    }
    logger->debug("调试消息");
    logger->info("服务启动完成");
    logger->warn("警告消息");
    logger->error("错误消息");
    vna::server::flushRuntimeLog();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    return run(argc, argv);
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif

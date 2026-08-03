#pragma once

#if defined(__GNUC__) && __GNUC__ < 8
#include <experimental/filesystem>
#else
#include <filesystem>
#endif

namespace vna::compat {

#if defined(__GNUC__) && __GNUC__ < 8
namespace filesystem = std::experimental::filesystem;
#else
namespace filesystem = std::filesystem;
#endif

[[nodiscard]] filesystem::path lexicallyNormal(const filesystem::path& input);

}  // namespace vna::compat

#include <vna/compat/filesystem.hpp>

namespace vna::compat {

filesystem::path lexicallyNormal(const filesystem::path& input) {
    filesystem::path result;
    for (const auto& part : input) {
        if (part == filesystem::path{"."}) {
            continue;
        }
        if (part != filesystem::path{".."}) {
            result /= part;
            continue;
        }
        if (!result.empty() && result != result.root_path() &&
            result.filename() != filesystem::path{".."}) {
            result = result.parent_path();
        } else if (!input.is_absolute()) {
            result /= part;
        }
    }
    if (!input.empty() && result.empty()) {
        return filesystem::path{"."};
    }
    return result;
}

}  // namespace vna::compat

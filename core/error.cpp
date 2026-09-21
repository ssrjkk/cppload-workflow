// @author ssrjkk | cppload
#include <cppload/error.hpp>

namespace cppload {

const std::error_category& err_category() {
    static ErrCategory category;
    return category;
}

} // namespace cppload
#pragma once

#include <unicorn/unicorn.h>

#include <string>
#include <system_error>

namespace vlinux {
class UcErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "unicorn"; }

    std::string message(int ev) const override {
        return uc_strerror(static_cast<uc_err>(ev));
    }
};

inline const std::error_category& uc_error_category() {
    static UcErrorCategory instance;
    return instance;
}

inline std::error_code make_error_code(uc_err e) {
    return {static_cast<int>(e), uc_error_category()};
}

}  // namespace vlinux

namespace std {
template <>
struct is_error_code_enum<uc_err> : true_type {};
}  // namespace std

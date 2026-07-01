// :error partition — parse/conversion errors.

module;

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

export module ZYaml:error;

export namespace zyaml {

struct Location {
    std::size_t line = 0;     // 1-based
    std::size_t column = 0;   // 1-based
    std::size_t offset = 0;  // 0-based byte offset into source
};

enum class YamlErrorCode {
    UnexpectedToken,
    BadIndent,
    BadEscape,
    UnclosedQuote,
    UnclosedFlow,
    DuplicateAnchor,
    UnknownAnchor,
    InvalidMergeTarget,
    BadTag,
    ScalarConversion,
    TypeMismatch,
    OutOfRange,
    IoError,
    EmptyInput,
};

struct YamlError {
    YamlErrorCode code = YamlErrorCode::UnexpectedToken;
    Location where{};
    std::string message;

    YamlError() = default;
    YamlError(YamlErrorCode c, Location w, std::string m)
        : code(c), where(std::move(w)), message(std::move(m)) {}

    [[nodiscard]] std::string format() const {
        std::string s = "zyaml error at line ";
        s += std::to_string(where.line);
        s += ":";
        s += std::to_string(where.column);
        s += ": ";
        s += message;
        return s;
    }
};

// A simple Result<T> wrapper around std::variant. MSVC 19.44 has a
// known issue instantiating std::expected's converting constructor when
// both template args are module-exported types (C2028). Result sidesteps
// it with explicit constructors. This is the permanent API, not a
// temporary workaround — std::expected's module interaction is a
// compiler bug, not a library design choice.
template <class T>
class Result {
public:
    Result(T v) : value_(std::move(v)) {}
    Result(YamlError e) : value_(std::move(e)) {}

    [[nodiscard]] bool has_value() const noexcept { return value_.index() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] const T& value() const { return std::get<0>(value_); }
    [[nodiscard]] const YamlError& error() const { return std::get<1>(value_); }

    [[nodiscard]] const T* operator->() const { return &std::get<0>(value_); }
    [[nodiscard]] T* operator->() { return &std::get<0>(value_); }
    [[nodiscard]] const T& operator*() const& { return std::get<0>(value_); }
    [[nodiscard]] T& operator*() & { return std::get<0>(value_); }
    [[nodiscard]] T operator*() && { return std::move(std::get<0>(value_)); }

private:
    std::variant<T, YamlError> value_;
};

} // namespace zyaml

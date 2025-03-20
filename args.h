#pragma once

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace arg {

namespace {

inline bool strEmpty(const char *sv) { return !(sv && sv[0]); }
inline std::string strToLower(std::string_view str) {
  std::string ret;
  ret.reserve(str.size());
  std::transform(str.begin(), str.end(), std::back_inserter(ret),
                 [](char c) { return std::tolower(c); });
  return ret;
}

inline constexpr bool lowerCmp(std::string_view sv1, std::string_view sv2) {
  return std::equal(sv1.begin(), sv1.end(), sv2.begin(), sv2.end(), [](char a, char b) {
    return std::tolower(a) == std::tolower(b);
  });
}

} // namespace

struct Arg {
  using ParserType = bool (*)(const Arg &arg, const char *value,
                              bool useDefault);
  using FormatterType = void (*)(const Arg &arg, std::ostream &os,
                                 bool printDefault);
  char shortFlag;
  bool hasDefault = false;
  const char *longFlag = nullptr;
  void *dst = nullptr;
  char defaultStorage[sizeof(uint64_t)] = {0};
  const char *desc = nullptr;
  ParserType parser;
  FormatterType formatter;
  // default value storage

  std::runtime_error formatError(std::string_view msg) const {
    std::stringstream ss;
    ss << "Error when parsing " << getFlag() << ": " << msg;
    return std::runtime_error(ss.str());
  }

  std::runtime_error formatError(std::string_view msg,
                                 std::string_view val) const {
    std::stringstream ss;
    ss << "Error when parsing '" << val << "' for " << getFlag() << ": " << msg;
    return std::runtime_error(ss.str());
  }

public:
  template <typename T>
  std::enable_if_t<sizeof(T) <= sizeof(defaultStorage), T &> getDefault() {
    return *reinterpret_cast<T *>(defaultStorage);
  }
  template <typename T>
  std::enable_if_t<sizeof(T) <= sizeof(defaultStorage), const T &>
  getDefault() const {
    return *reinterpret_cast<const T *>(defaultStorage);
  }
  std::string getFlag() const {
    if (shortFlag) {
      return std::string{"-"} + shortFlag;
    } else {
      return std::string("--") + longFlag;
    }
  }
  void parseValue(const char *value) const {
    if (!parser(*this, value, false)) {
      throw formatError("failed to parse value", value);
    }
  }
  void setDefault() const {
    if (!hasDefault) {
      throw formatError("does not have a default value");
    }
    if (!parser(*this, nullptr, true)) {
      throw formatError("failed to set default value");
    }
  }
  void printVal(std::ostream &os) const { formatter(*this, os, false); }

  void usage(std::ostream &out) const {
    if (shortFlag) {
      out << "  -" << shortFlag;
      if (!strEmpty(longFlag)) {
        out << "(--" << longFlag << ')';
      }
    } else {
      out << "  --" << longFlag;
    }
    if (hasDefault) {
      out << " [default = ";
      formatter(*this, out, true);
      out << "]\n";
    } else {
      out << " [required]\n";
    }
    if (desc) {
      out << '\t' << desc << '\n';
    }
  }
};

namespace {

template <typename T>
inline std::enable_if_t<std::is_arithmetic_v<T>, bool>
numericParser(const Arg &arg, const char *value, bool useDefault) {
  static constexpr size_t kMaxArgLen = 128UL;
  T result;
  if (strEmpty(value)) {
    if (!useDefault) {
      return false;
    }
    result = arg.getDefault<T>();
  } else {
    if constexpr (std::is_integral_v<T>) {
      auto [ptr, ec] =
          std::from_chars(value, value + strnlen(value, kMaxArgLen), result);
      if (ec != std::errc()) {
        return false;
      }
    } else {
      // older compilers' implementation of std::from_chars does not support
      // floating point values *sigh*
      auto valEnd = const_cast<char *>(value + strnlen(value, kMaxArgLen));
      if constexpr (std::is_same_v<T, float>) {
        result = strtof(value, &valEnd);
      } else if constexpr (std::is_same_v<T, double>) {
        result = strtod(value, &valEnd);
      } else {
        result = strtold(value, &valEnd);
      }
      if (value == valEnd) {
        return false;
      }
    }
  }
  *static_cast<T *>(arg.dst) = result;
  return true;
}

template <typename T>
inline void genericFormatter(const Arg &arg, std::ostream &os,
                             bool printDefault) {
  if (printDefault) {
    // special handling for string default
    if constexpr (std::is_same_v<T, std::string>) {
      os << arg.getDefault<const char *>();
    } else {
      os << arg.getDefault<T>();
    }
  } else {
    os << *static_cast<const T *>(arg.dst);
  }
}

template <typename T>
inline std::enable_if_t<std::is_integral_v<T>, bool>
sizeParser(const Arg &arg, const char *value, bool useDefault) {
  T result = 0;
  if (strEmpty(value)) {
    if (!useDefault) {
      return false;
    }
    result = arg.getDefault<T>();
  } else {
    std::string v = strToLower(value);

    // Find first non-digit character
    size_t pos = 0;
    while (pos < v.length() && (std::isdigit(v[pos]) || v[pos] == '.')) {
      pos++;
    }

    // Parse base number
    char *vEnd = v.data() + pos;
    double base = strtod(v.data(), &vEnd);
    if (v.data() == vEnd) {
      return false;
    }

    // Apply multiplier based on suffix
    std::string suffix = strToLower(v.substr(pos));
    if (v.ends_with("k") || v.ends_with("kb")) {
      result = static_cast<T>(base * 1024);
    } else if (v.ends_with("m") || v.ends_with("mb")) {
      result = static_cast<T>(base * 1024 * 1024);
    } else if (v.ends_with("g") || v.ends_with("gb")) {
      result = static_cast<T>(base * 1024ULL * 1024ULL * 1024ULL);
    } else if (v.ends_with("t") || v.ends_with("tb")) {
      result = static_cast<T>(base * 1024ULL * 1024ULL * 1024ULL * 1024ULL);
    } else if (suffix.empty() || v.ends_with("b")) {
      result = static_cast<T>(base);
    } else {
      return false;
    }
  }
  *static_cast<T *>(arg.dst) = result;
  return true;
}

template <typename T>
inline std::enable_if_t<std::is_integral_v<T>, void>
sizeFormatter(const Arg &arg, std::ostream &os, bool printDefault) {
  // Format file size with appropriate suffix based on magnitude
  const T *source =
      static_cast<const T *>(printDefault ? arg.defaultStorage : arg.dst);

  // Use double for division to preserve precision
  double size = static_cast<double>(*source);

  // Find appropriate suffix by dividing by 1024 until small enough
  const char *suffixes[] = {"B", "KB", "MB", "GB", "TB"};
  int suffix_idx = 0;

  while (size >= 1024 && suffix_idx < 4) {
    size /= 1024;
    suffix_idx++;
  }

  // If size is integral (no decimal places), print without decimals
  if (size == static_cast<T>(size)) {
    os << static_cast<unsigned long long>(size);
  } else {
    os << std::fixed << std::setprecision(2) << size;
  }
  os << suffixes[suffix_idx];
}

inline bool boolParser(const Arg &arg, const char *value, bool useDefault) {
  bool result;
  if (!value) {
    if (useDefault) {
      result = arg.getDefault<bool>();
    } else {
      // only flag and no value means setting the value to true
      result = true;
    }
  } else {
    std::string v = strToLower(value);
    if (v == "1" || v == "yes" || v == "y" || v == "true") {
      result = true;
    } else if (v == "0" || v == "no" || v == "n" || v == "false") {
      result = false;
    } else {
      return false;
    }
  }
  *static_cast<bool *>(arg.dst) = result;
  return true;
}

inline void boolFormatter(const Arg &arg, std::ostream &os, bool printDefault) {
  const bool &source = printDefault ? arg.getDefault<bool>()
                                    : *static_cast<const bool *>(arg.dst);
  os << (source ? "true" : "false");
}

inline bool charParser(const Arg &arg, const char *value, bool useDefault) {
  if (strEmpty(value)) {
    if (!useDefault) {
      return false;
    }
    *static_cast<char *>(arg.dst) = arg.getDefault<char>();
  } else {
    *static_cast<char *>(arg.dst) = value[0];
  }
  return true;
}

inline bool strParser(const Arg &arg, const char *value, bool useDefault) {
  if (strEmpty(value)) {
    if (!useDefault) {
      return false;
    }
    *static_cast<std::string *>(arg.dst) = arg.getDefault<const char *>();
  } else {
    *static_cast<std::string *>(arg.dst) = value;
  }
  return true;
}

template <typename E, E V>
consteval std::optional<std::string_view> enumToString() {
  std::string_view name(__PRETTY_FUNCTION__);
  auto prefix = name.find("V = ") + 4;
  auto nameIdx = name.find("::", prefix);
  if (nameIdx == std::string_view::npos) {
    return std::nullopt;
  }
  auto suffix = name.find(']', nameIdx + 2);
  return name.substr(nameIdx + 2, suffix - (nameIdx + 2));
}

template <typename E, size_t... Vs>
consteval size_t enumValidCountImpl(std::index_sequence<Vs...>) {
  size_t ret{0};
  ((ret += enumToString<E, static_cast<E>(Vs)>().has_value()), ...);
  return ret;
}

template <typename E, size_t BSize = 64> consteval auto enumValidCount() {
  return enumValidCountImpl<E>(std::make_index_sequence<BSize>{});
}

// std::bitset is not constexpr until C++23, use array of bools instead
template <typename E, size_t... Vs>
consteval auto enumValidSetImpl(std::index_sequence<Vs...> seq) {
  std::array<bool, seq.size()> ret;
  ((ret[Vs] = enumToString<E, static_cast<E>(Vs)>().has_value()), ...);
  return ret;
}

template <typename E, size_t BSize = 64> consteval auto enumValidSet() {
  return enumValidSetImpl<E>(std::make_index_sequence<BSize>{});
}

template <typename E, size_t... Vs>
consteval auto enumLookupTableImpl(std::index_sequence<Vs...> seq) {
  std::array<std::pair<std::string_view, E>, enumValidCountImpl<E>(seq)>
      result{};
  size_t index = 0;
  ((enumToString<E, static_cast<E>(Vs)>().has_value()
        // for older compiler versions operator= for std::pair is not constexpr 
        ? (result[index].first = enumToString<E, static_cast<E>(Vs)>().value(),
           result[index].second = static_cast<E>(Vs), index++, 0)
        : 0),
   ...);
  return result;
}

template <typename E, size_t BSize = 64> consteval auto enumLookupTable() {
  return enumLookupTableImpl<E>(std::make_index_sequence<BSize>());
}

template <typename E>
inline std::enable_if_t<std::is_enum_v<E>, bool>
enumParser(const Arg &arg, const char *value, bool useDefault) {
  constexpr size_t kMaxArgLen = 16;
  if (strEmpty(value)) {
    if (!useDefault) {
      return false;
    }
    *static_cast<E *>(arg.dst) = arg.getDefault<E>();
    return true;
  } else {
    // parse numeric value
    std::underlying_type_t<E> result;
    auto [ptr, ec] =
        std::from_chars(value, value + strnlen(value, kMaxArgLen), result);
    if (ec == std::errc()) {
      const auto &validSet = enumValidSet<E>();
      if (result < validSet.size() && enumValidSet<E>()[result]) {
        *static_cast<E *>(arg.dst) = static_cast<E>(result);
        return true;
      }
      return false;
    }
    // parse string value
    for (const auto &p : enumLookupTable<E>()) {
      if (lowerCmp(p.first, value)) {
        *static_cast<E *>(arg.dst) = p.second;
        return true;
      }
    }
    return false;
  }
}

template <typename E>
inline std::enable_if_t<std::is_enum_v<E>, void>
enumFormatter(const Arg &arg, std::ostream &os, bool printDefault) {
  const E &source =
      printDefault ? arg.getDefault<E>() : *static_cast<const E *>(arg.dst);
  for (const auto &p : enumLookupTable<E>()) {
    if (p.second == source) {
      os << p.first;
      return;
    }
  }
  os << std::underlying_type_t<E>(source);
}
} // namespace

#define DEFINE_ARG(_name, _type, _deftype, _parser, _formatter)                \
  inline constexpr Arg _name##Arg(                                             \
      char shortFlag, _type &dst, const char *longFlag = nullptr,              \
      std::optional<_deftype> defVal = std::nullopt,                           \
      const char *desc = nullptr) {                                            \
    Arg ret{.shortFlag = shortFlag,                                            \
            .hasDefault = defVal.has_value(),                                  \
            .longFlag = longFlag,                                              \
            .dst = static_cast<void *>(&dst),                                  \
            .desc = desc,                                                      \
            .parser = _parser,                                                 \
            .formatter = _formatter};                                          \
    if (defVal.has_value()) {                                                  \
      ret.getDefault<_deftype>() = *defVal;                                    \
    }                                                                          \
    return ret;                                                                \
  };

#define NARG(_name, _type)                                                     \
  DEFINE_ARG(_name, _type, _type, numericParser<_type>, genericFormatter<_type>)

NARG(Int8, int8_t)
NARG(Int16, int16_t)
NARG(Int, int32_t)
NARG(Int64, int64_t)
NARG(UInt8, uint8_t)
NARG(UInt16, uint16_t)
NARG(UInt, uint32_t)
NARG(UInt64, uint64_t)
NARG(Float, float)
NARG(Double, double)

#undef NARG

DEFINE_ARG(Bool, bool, bool, boolParser, boolFormatter)
DEFINE_ARG(Char, char, char, charParser, genericFormatter<char>)
DEFINE_ARG(String, std::string, const char *, strParser,
           genericFormatter<std::string>)
DEFINE_ARG(Size, uint64_t, uint64_t, sizeParser<uint64_t>,
           sizeFormatter<uint64_t>)
template <typename E>
DEFINE_ARG(Enum, E, E, enumParser<E>, enumFormatter<E>)

#undef DEFINE_ARG

class Parser {
public:
  static constexpr char kHelpFlag[] = "-h";
  static constexpr char kHelpLongFlag[] = "--help";
  Parser(std::initializer_list<Arg> args) : args(args) {}
  void parse(int argc, const char *argv[]) {
    std::vector<unsigned char> parsed(args.size(), false);
    // skip first argument if it's not a flag(possibly application name)
    for (int i = isFlag(argv[0]) ? 0 : 1; i < argc; i++) {
      if (!isFlag(argv[i])) {
        throw std::invalid_argument(
            std::string("Parsing error: invalid argument ") + argv[i]);
      }

      auto it = args.cend();
      if (argv[i][1] == '-') {
        // long flag handling
        const char *longFlag = argv[i] + 2;
        it = std::find_if(args.begin(), args.end(), [=](const Arg &arg) {
          return arg.longFlag && arg.longFlag == longFlag;
        });
      } else {
        // short flag handling
        char shortFlag = argv[i][1];
        it = std::find_if(args.begin(), args.end(), [=](const Arg &arg) {
          return arg.shortFlag && arg.shortFlag == shortFlag;
        });
      }
      if (it == args.cend()) {
        // special handling for -h and --help
        if (strcmp(argv[i], kHelpFlag) == 0 ||
            strcmp(argv[i], kHelpLongFlag) == 0) {
          usage(std::cerr, !isFlag(argv[0]) ? argv[0] : nullptr);
          exit(0);
        } else {
          throw std::invalid_argument(std::string("Unknown flag: ") + argv[i]);
        }
      }

      auto &parseFlag = parsed[std::distance(args.cbegin(), it)];
      if (parseFlag) {
        throw std::invalid_argument(std::string("Duplicate flag: ") + argv[i]);
      }
      parseFlag = true;

      // pass next argument as value(if it exists and is not a flag)
      const char *value = nullptr;
      if (i + 1 < argc && !isFlag(argv[i + 1])) {
        value = argv[i + 1];
        i++;
      }
      it->parseValue(value);
    }
    // set all unparsed value to default
    for (unsigned idx = 0; idx < args.size(); idx++) {
      if (!parsed[idx]) {
        args[idx].setDefault();
      }
    }
  }

  void printAll(std::ostream &os) {
    os << "values:\n";
    for (const Arg &arg : args) {
      os << "  -" << arg.shortFlag;
      if (!strEmpty(arg.longFlag)) {
        os << "(--" << arg.longFlag << ')';
      }
      os << '\t';
      arg.printVal(os);
      os << '\n';
    }
  }

  void usage(std::ostream &os, const char *programName) {
    if (!programName) {
      programName = "(program name not provided)";
    }
    os << "Usage: " << programName << "\n";
    for (const Arg &arg : args) {
      arg.usage(os);
    }
    os << "  -h(--help)\n"
       << "\tprint this help message" << std::endl;
  }

private:
  bool isFlag(const char *str) { return str && str[0] == '-'; }
  std::vector<Arg> args;
};

#define VALIDATE(_pred)                                                        \
  do {                                                                         \
    if (__builtin_expect(!(_pred), 0))                                         \
      throw std::runtime_error("validation failed: " #_pred);                  \
  } while (0)

} // namespace arg
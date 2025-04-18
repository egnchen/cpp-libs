#pragma once

#include <charconv>
#include <cstdlib>
#include <cstring>
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

inline constexpr bool isFlag(const char *str) { return str && str[0] == '-'; }

inline constexpr bool lowerCmp(std::string_view sv1, std::string_view sv2) {
  return std::equal(
      sv1.begin(), sv1.end(), sv2.begin(), sv2.end(),
      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

template <typename T>
std::enable_if_t<std::is_arithmetic_v<T>, std::optional<T>>
numericParser(const char *value) {
  static constexpr size_t kMaxArgLen = 128UL;
  T result;
  if (strEmpty(value)) {
    return {};
  } else {
    if constexpr (std::is_integral_v<T>) {
      auto [ptr, ec] =
          std::from_chars(value, value + strnlen(value, kMaxArgLen), result);
      if (ec != std::errc()) {
        return {};
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
        return {};
      }
    }
  }
  return result;
}

template <typename T>
std::enable_if_t<std::is_integral_v<T>, std::optional<T>>
sizeParser(const char *value) {
  T result = 0;
  if (strEmpty(value)) {
    return {};
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
      return {};
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
      return {};
    }
  }
  return result;
}

template <typename T>
std::enable_if_t<std::is_integral_v<T>, void> sizeFormatter(std::ostream &os,
                                                            const T &val) {
  // Format file size with appropriate suffix based on magnitude

  // Use double for division to preserve precision
  double size = static_cast<double>(val);

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

std::optional<bool> boolParser(const char *value) {
  bool result;
  if (strEmpty(value)) {
    // no value means the value is set
    return true;
  } else {
    std::string v = strToLower(value);
    if (v == "1" || v == "yes" || v == "y" || v == "true") {
      result = true;
    } else if (v == "0" || v == "no" || v == "n" || v == "false") {
      result = false;
    } else {
      return {};
    }
  }
  return result;
}

void boolFormatter(std::ostream &os, const bool &val) {
  os << (val ? "true" : "false");
}

std::optional<char> charParser(const char *value) {
  return strEmpty(value) ? std::optional<char>{} : value[0];
}

std::optional<std::string> strParser(const char *value) {
  return strEmpty(value) ? std::optional<std::string>{} : value;
};

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

template <typename E, size_t BSize = 64>
std::enable_if_t<std::is_enum_v<E>, std::optional<E>>
enumParser(const char *value) {
  constexpr size_t kMaxArgLen = 16;
  if (strEmpty(value)) {
    return {};
  } else {
    // parse numeric value
    std::underlying_type_t<E> result;
    auto [ptr, ec] =
        std::from_chars(value, value + strnlen(value, kMaxArgLen), result);
    if (ec == std::errc()) {
      const auto &validSet = enumValidSet<E, BSize>();
      if (static_cast<size_t>(result) < validSet.size() &&
          enumValidSet<E>()[result]) {
        return static_cast<E>(result);
      }
      return {};
    }
    // parse string value
    for (const auto &p : enumLookupTable<E, BSize>()) {
      if (lowerCmp(p.first, value)) {
        return p.second;
      }
    }
    return {};
  }
}

template <typename E, size_t BSize = 64>
std::enable_if_t<std::is_enum_v<E>, void> enumFormatter(std::ostream &os,
                                                        const E &val) {
  for (const auto &p : enumLookupTable<E, BSize>()) {
    if (p.second == val) {
      os << p.first;
      return;
    }
  }
  os << std::underlying_type_t<E>(val);
}

template <typename T> void genericFormatter(std::ostream &os, const T &val) {
  os << val;
}

} // namespace

struct ArgBase {
  char shortFlag;
  bool hasDefault;
  void *dst;
  const char *longFlag = nullptr;
  const char *desc = nullptr;

  ArgBase(char shortFlag, bool hasDefault, void *dst,
          const char *longFlag = nullptr, const char *desc = nullptr)
      : shortFlag(shortFlag), hasDefault(hasDefault), dst(dst),
        longFlag(longFlag), desc(desc) {
    if (dst == nullptr) {
      std::stringstream ss;
      ss << "dst for ";
      printFlag(ss);
      ss << " not specified";
      throw std::runtime_error(ss.str());
    }
  }
  virtual ~ArgBase() {}

  template <typename T> T *getDst() const { return reinterpret_cast<T *>(dst); }
  virtual void parseValue(const char *) = 0;
  virtual void formatValue(std::ostream &) const = 0;
  virtual void setDefault() {
    if (!hasDefault) {
      std::stringstream ss;
      ss << "cannot set default for ";
      printFlag(ss);
      ss << "(value required)";
      throw std::runtime_error(ss.str());
    }
  }
  virtual void usage(std::ostream &) const = 0;

  void printFlag(std::ostream &os) const {
    if (shortFlag) {
      os << "  -" << shortFlag;
      if (!strEmpty(longFlag)) {
        os << "(--" << longFlag << ')';
      }
    } else {
      os << "  --" << longFlag;
    }
  }
  void printDesc(std::ostream &os) const {
    if (desc) {
      os << '\t' << desc << '\n';
    }
  }
};

template <typename T> struct Arg : public ArgBase {
  T defVal;
  Arg(char shortFlag, T &dst, const char *longFlag,
      std::optional<T> defVal = std::nullopt, const char *desc = nullptr)
      : ArgBase(shortFlag, defVal.has_value(), static_cast<void *>(&dst),
                longFlag, desc) {
    if (defVal.has_value())
      this->defVal = *defVal;
  }

private:
  void doFormat(std::ostream &os, const T &val) const {
    if constexpr (std::is_enum_v<T>) {
      enumFormatter(os, val);
    } else if constexpr (std::is_same_v<bool, T>) {
      boolFormatter(os, val);
    } else {
      genericFormatter(os, val);
    }
  }

public:
  virtual void parseValue(const char *value) override {
    try {
      if constexpr (std::is_same_v<T, bool>) {
        *getDst<T>() = boolParser(value).value();
      } else if constexpr (std::is_arithmetic_v<T>) {
        *getDst<T>() = numericParser<T>(value).value();
      } else if constexpr (std::is_enum_v<T>) {
        *getDst<T>() = enumParser<T>(value).value();
      } else if constexpr (std::is_same_v<T, char>) {
        *getDst<T>() = charParser(value).value();
      } else if constexpr (std::is_same_v<T, std::string>) {
        *getDst<T>() = strParser(value).value();
      } else {
        static_assert(sizeof(T), "no default parser for type");
      }
    } catch (const std::bad_optional_access &e) {
      std::cerr << "Failed to parse argument ";
      printFlag(std::cerr);
      std::cerr << "\n" << "Argument value: " << value << std::endl;
    }
  }

  virtual void setDefault() override {
    ArgBase::setDefault();
    *getDst<T>() = defVal;
  }

  virtual void formatValue(std::ostream &os) const override {
    doFormat(os, *getDst<T>());
  }

  virtual void usage(std::ostream &os) const override {
    printFlag(os);
    if (hasDefault) {
      os << " [default = ";
      doFormat(os, defVal);
      os << "]\n";
    } else {
      os << " [required]\n";
    }
    if constexpr (std::is_enum_v<T>) {
      os << "possible values: ";
      bool first = true;
      for (const auto &kv : enumLookupTable<T>()) {
        if (!first) {
          os << ", ";
        }
        os << kv.first << "("
           << static_cast<std::underlying_type_t<T>>(kv.second) << ")";
        first = false;
      }
      os << "\n";
    }
    printDesc(os);
  }
};

template <typename T>
Arg(char, T &, const char *, std::nullopt_t, const char * = nullptr)
    -> Arg<std::decay_t<T>>;

Arg(char, std::string &, const char *, const char *, const char * = nullptr)
    -> Arg<std::string>;

// Deduction guide for Arg<T> when defVal is provided as T directly
template <typename T>
Arg(char, T &, const char *, T, const char * = nullptr) -> Arg<std::decay_t<T>>;

template <typename T> struct SizeArg : public Arg<T> {
  static_assert(std::is_integral_v<T>);
  SizeArg(char shortFlag, T &dst, const char *longFlag,
          std::optional<T> defVal = std::nullopt, const char *desc = nullptr)
      : Arg<T>(shortFlag, dst, longFlag, defVal, desc) {}

  void parseValue(const char *value) override {
    try {
      *ArgBase::getDst<T>() = sizeParser<T>(value).value();
    } catch (const std::bad_optional_access &e) {
      std::cerr << "Failed to parse argument ";
      ArgBase::printFlag(std::cerr);
      std::cerr << "\n" << "Argument value: " << value << std::endl;
    }
  }

  void formatValue(std::ostream &os) const override {
    sizeFormatter(os, *ArgBase::getDst<T>());
  }

  void usage(std::ostream &os) const override {
    ArgBase::printFlag(os);
    if (ArgBase::hasDefault) {
      os << " [default = ";
      sizeFormatter(os, Arg<T>::defVal);
      os << "]\n";
    } else {
      os << " [required]\n";
    }
    ArgBase::printDesc(os);
  }
};

template <typename T>
SizeArg(char, T &, const char *, std::nullopt_t, const char * = nullptr)
    -> SizeArg<std::decay_t<T>>;

// Deduction guide for SizeArg<T> when defVal is provided as T directly
template <typename T>
SizeArg(char, T &, const char *, T, const char * = nullptr)
    -> SizeArg<std::decay_t<T>>;

template <typename... Args> class Parser {
public:
  std::array<ArgBase *, sizeof...(Args)> args;
  std::tuple<Args...> storage;
  static constexpr char HELP_FLAG = 'h';
  static constexpr const char HELP_LONG_FLAG[] = "help";
  static constexpr uintptr_t ALIGNMENT = 4;

  Parser(Args &&...argList) : storage(std::forward<Args>(argList)...) {
    [&]<size_t... Vs>(std::index_sequence<Vs...> seq) {
      ((args[Vs] = &std::get<Vs>(storage), 0), ...);
    }(std::make_index_sequence<sizeof...(Args)>());
    doCheckFlags();
  }

  size_t size() const { return args.size(); }

public:
  void parse(int argc, const char *argv[]) const {
    auto sz = size();
    std::vector<unsigned char> parsed(sz, false);
    for (int i = isFlag(argv[0]) ? 0 : 1; i < argc; i++) {
      const char *argv0 = argv[i];
      const char *argv1 = nullptr;
      if (i + 1 < argc && !isFlag(argv[i + 1])) {
        argv1 = argv[++i];
      }
      bool isLongFlag = argv0[1] == '-';
      int matchIdx = -1;
      for (size_t j = 0; j < sz; j++) {
        if (isLongFlag) {
          if (args[j]->longFlag && strcmp(argv0, args[j]->longFlag) == 0) {
            matchIdx = j;
            break;
          }
        } else if (args[j]->shortFlag && args[j]->shortFlag == argv0[1]) {
          matchIdx = j;
          break;
        }
      }
      if (matchIdx < 0) {
        // special handling for -h and --help
        if (argv[i][1] == HELP_FLAG ||
            (argv[i][1] == '-' && strcmp(HELP_LONG_FLAG, argv[i] + 2) == 0)) {
          usage(std::cerr, !isFlag(argv[0]) ? argv[0] : "");
          exit(0);
        } else {
          throw std::invalid_argument(std::string("Unknown flag: ") + argv[i]);
        }
      } else if (parsed[matchIdx]) {
        throw std::invalid_argument(std::string("Duplicate flag: ") + argv[i]);
      } else {
        args[matchIdx]->parseValue(argv1);
        parsed[matchIdx] = true;
      }
    }

    // set default values
    for (size_t i = 0; i < parsed.size(); i++) {
      if (!parsed[i]) {
        args[i]->setDefault();
      }
    }
  }

  void printAll(std::ostream &os) const {
    os << "values:\n";
    for (size_t i = 0; i < size(); i++) {
      const auto *arg = args[i];
      arg->printFlag(os);
      os << '\t';
      arg->formatValue(os);
      os << '\n';
    }
    os.flush();
  }

  void usage(std::ostream &os, std::string_view programName) const {
    if (programName.empty()) {
      programName = "(program name not provided)";
    }
    os << "Usage: " << programName << "\n";
    for (size_t i = 0; i < size(); i++) {
      args[i]->usage(os);
    }
    os << "  -h(--help)\n"
       << "\tprint this help message" << std::endl;
  }

private:
  void doCheckFlags() {
    for (size_t i = 0; i < size(); i++) {
      if (args[i]->shortFlag) {
        if (args[i]->shortFlag == HELP_FLAG) {
          throw std::invalid_argument("cannot manually set -h flag");
        }
        for (size_t j = i + 1; j < size(); j++) {
          if (args[i]->shortFlag == args[j]->shortFlag) {
            throw std::invalid_argument(std::string("duplicate flag: -") +
                                        args[i]->shortFlag);
          }
        }
      }
      if (args[i]->longFlag) {
        if (strcmp(HELP_LONG_FLAG, args[i]->longFlag) == 0) {
          throw std::invalid_argument("cannot manually set --help flag");
        }
        for (size_t j = i + 1; j < size(); j++) {
          if (args[j]->longFlag &&
              strcmp(args[i]->longFlag, args[j]->longFlag) == 0) {
            throw std::invalid_argument(std::string("duplicate flag: --") +
                                        args[i]->longFlag);
          }
        }
      }
    }
  }
};

#define VALIDATE(_pred)                                                        \
  do {                                                                         \
    if (__builtin_expect(!(_pred), 0))                                         \
      throw std::runtime_error("validation failed: " #_pred);                  \
  } while (0)

} // namespace arg

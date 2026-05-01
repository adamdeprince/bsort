#include <algorithm>
#include <array>
#include <cassert>
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bsort/bsort.hpp"

namespace {

#ifndef BSORT_USE_STD_SORT_CLEANUP
#define BSORT_USE_STD_SORT_CLEANUP 1
#endif

#ifndef BSORT_STD_SORT_CLEANUP_MIN
#define BSORT_STD_SORT_CLEANUP_MIN 96
#endif

constexpr bool use_std_sort_cleanup = BSORT_USE_STD_SORT_CLEANUP != 0;
constexpr std::size_t default_std_sort_cleanup_min =
    BSORT_STD_SORT_CLEANUP_MIN;

using Byte = std::byte;

enum class Advice {
  normal,
  random,
  sequential,
  willneed,
};

enum class Allocation {
  truncate,
  fallocate,
};

struct Options {
  bool verbose = false;
  bool sync_after_sort = false;
  std::uint16_t char_start = 0;
  std::uint16_t char_stop = 255;
  std::size_t record_size = 100;
  std::size_t key_size = 8;
  std::size_t stack_size = 16;
  std::size_t cut_off = 500;
  std::size_t std_sort_cleanup_min = default_std_sort_cleanup_min;
  Allocation output_allocation = Allocation::truncate;
  Advice input_advice = Advice::sequential;
  Advice output_advice = Advice::sequential;
  std::optional<std::filesystem::path> input_path;
  std::optional<std::filesystem::path> output_path;
  std::vector<std::filesystem::path> positional_paths;
};

class MappedFile {
 public:
  MappedFile() = default;

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  MappedFile(MappedFile&& other) noexcept {
    move_from(other);
  }

  MappedFile& operator=(MappedFile&& other) noexcept {
    if (this != &other) {
      close();
      move_from(other);
    }
    return *this;
  }

  ~MappedFile() {
    close();
  }

  [[nodiscard]] static MappedFile open_existing(const std::filesystem::path& path,
                                                const bool writable,
                                                const Advice advice) {
    const int flags = writable ? O_RDWR : O_RDONLY;
    const int fd = ::open(path.c_str(), flags);
    if (fd == -1) {
      throw_system_error("open", path);
    }

    struct stat stats {};
    if (::fstat(fd, &stats) == -1) {
      const int saved_errno = errno;
      ::close(fd);
      throw_system_error("fstat", path, saved_errno);
    }
    if (stats.st_size < 0) {
      ::close(fd);
      throw std::runtime_error("negative file size for " + path.string());
    }

    MappedFile file;
    file.fd_ = fd;
    file.path_ = path;
    file.size_ = static_cast<std::size_t>(stats.st_size);
    file.writable_ = writable;
    file.map(advice);
    return file;
  }

  [[nodiscard]] static MappedFile create_sized(const std::filesystem::path& path,
                                               const std::size_t size,
                                               const Allocation allocation,
                                               const Advice advice) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
      throw_system_error("open", path);
    }

    if (::ftruncate(fd, static_cast<off_t>(size)) == -1) {
      const int saved_errno = errno;
      ::close(fd);
      throw_system_error("ftruncate", path, saved_errno);
    }

    if (allocation == Allocation::fallocate && size != 0) {
      const int result = ::posix_fallocate(fd, 0, static_cast<off_t>(size));
      if (result != 0) {
        ::close(fd);
        throw_system_error("posix_fallocate", path, result);
      }
    }

    MappedFile file;
    file.fd_ = fd;
    file.path_ = path;
    file.size_ = size;
    file.writable_ = true;
    file.map(advice);
    return file;
  }

  [[nodiscard]] std::span<Byte> bytes() {
    return {data_, size_};
  }

  [[nodiscard]] std::span<const Byte> bytes() const {
    return {data_, size_};
  }

  [[nodiscard]] std::size_t size() const {
    return size_;
  }

  void flush_sync() {
    if (writable_ && data_ != nullptr && size_ != 0) {
      if (::msync(data_, size_, MS_SYNC) == -1) {
        throw_system_error("msync", path_);
      }
    }
    ::sync();
  }

  void close() noexcept {
    if (data_ != nullptr) {
      ::munmap(data_, size_);
      data_ = nullptr;
    }
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }
    size_ = 0;
    writable_ = false;
  }

 private:
  static void throw_system_error(const std::string_view operation,
                                 const std::filesystem::path& path,
                                 const int error_code = errno) {
    throw std::system_error(error_code,
                            std::generic_category(),
                            std::string(operation) + " " + path.string());
  }

  [[nodiscard]] static int madvise_flags(const Advice advice) {
    switch (advice) {
      case Advice::normal:
        return POSIX_MADV_NORMAL;
      case Advice::random:
        return POSIX_MADV_RANDOM;
      case Advice::sequential:
        return POSIX_MADV_SEQUENTIAL;
      case Advice::willneed:
        return POSIX_MADV_WILLNEED;
    }
    return POSIX_MADV_NORMAL;
  }

  void map(const Advice advice) {
    if (size_ == 0) {
      return;
    }

    const int prot = writable_ ? (PROT_READ | PROT_WRITE) : PROT_READ;
    void* const mapped =
        ::mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
    if (mapped == MAP_FAILED) {
      const int saved_errno = errno;
      ::close(fd_);
      fd_ = -1;
      throw_system_error("mmap", path_, saved_errno);
    }

    data_ = static_cast<Byte*>(mapped);
    static_cast<void>(::madvise(data_, size_, madvise_flags(advice)));
  }

  void move_from(MappedFile& other) noexcept {
    fd_ = other.fd_;
    data_ = other.data_;
    size_ = other.size_;
    writable_ = other.writable_;
    path_ = std::move(other.path_);

    other.fd_ = -1;
    other.data_ = nullptr;
    other.size_ = 0;
    other.writable_ = false;
  }

  int fd_ = -1;
  Byte* data_ = nullptr;
  std::size_t size_ = 0;
  bool writable_ = false;
  std::filesystem::path path_;
};

[[nodiscard]] std::uint8_t to_uint8(const Byte value) {
  return static_cast<std::uint8_t>(value);
}

template <std::uint16_t CharStart, std::uint16_t CharStop>
struct Alphabet {
  static_assert(CharStart <= CharStop);
  static_assert(CharStop <= 255);

  static constexpr std::size_t first = CharStart;
  static constexpr std::size_t last = CharStop;
  static constexpr std::size_t width = CharStop - CharStart + 1;
  static constexpr bool needs_validation = width != 256;
  static constexpr std::size_t padded_stride =
      width == 256 ? 264 : ((width + 7) / 8) * 8;

  using Histogram = std::array<std::size_t, padded_stride>;
  using Histograms = std::array<Histogram, width>;

  [[nodiscard]] static std::size_t bin_for(const Byte value) {
    const std::uint8_t digit = to_uint8(value);
    assert(digit >= first && digit <= last);
    return digit - first;
  }

  [[nodiscard]] static std::size_t checked_bin_for(const Byte value) {
    const std::uint8_t digit = to_uint8(value);
    if constexpr (needs_validation) {
      if (digit < first || digit > last) {
        throw std::runtime_error(
            "record contains a key byte outside the configured alphabet");
      }
    }
    return digit - first;
  }
};

#ifndef BSORT_LIBRARY_ONLY
[[nodiscard]] std::size_t parse_positive_number(const char* value,
                                                const std::string_view name) {
  std::size_t parsed = 0;
  const std::string_view text(value);
  const auto* const begin = text.data();
  const auto* const end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc {} || ptr != end || parsed == 0) {
    throw std::invalid_argument(std::string(name) +
                                " must be a positive integer");
  }
  return parsed;
}

[[nodiscard]] std::size_t parse_nonnegative_number(const char* value,
                                                   const std::string_view name) {
  std::size_t parsed = 0;
  const std::string_view text(value);
  const auto* const begin = text.data();
  const auto* const end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc {} || ptr != end) {
    throw std::invalid_argument(std::string(name) +
                                " must be a non-negative integer");
  }
  return parsed;
}

[[nodiscard]] Advice parse_advice(const std::string_view value) {
  if (value == "normal") {
    return Advice::normal;
  }
  if (value == "random") {
    return Advice::random;
  }
  if (value == "sequential") {
    return Advice::sequential;
  }
  if (value == "willneed") {
    return Advice::willneed;
  }
  throw std::invalid_argument(
      "advice must be one of normal, random, sequential, willneed");
}

[[nodiscard]] Allocation parse_allocation(const std::string_view value) {
  if (value == "truncate") {
    return Allocation::truncate;
  }
  if (value == "fallocate") {
    return Allocation::fallocate;
  }
  throw std::invalid_argument("allocation must be one of truncate, fallocate");
}
#endif

void apply_ascii_defaults(Options& options,
                          const bool explicit_stack_size,
                          const bool explicit_cut_off,
                          const bool explicit_std_sort_threshold) {
  if (options.char_start == 32 && options.char_stop == 126) {
    if (!explicit_stack_size) {
      options.stack_size = 8;
    }
    if (!explicit_cut_off) {
      options.cut_off = 500;
    }
    if (!explicit_std_sort_threshold) {
      options.std_sort_cleanup_min = 256;
    }
  }
}

#ifndef BSORT_LIBRARY_ONLY
void print_usage(const char* program, std::ostream& out) {
  out << "Usage: " << program
      << " [-v] [-S] [-a] [-r ###] [-k ###] [-s ###] [-c ###] [-t ###]"
         " [-A mode] [-I advice] [-O advice] [-i infile] [-o outfile] <file>\n"
      << "\n"
      << "Sort fixed-width binary records by their key prefix.\n"
      << "\n"
      << "Sorting options:\n"
      << "  -a        assume printable 7-bit ASCII keys instead of binary keys\n"
      << "  -k ###    key size in bytes (default 8)\n"
      << "  -r ###    record size in bytes (default 100)\n"
      << "\n"
      << "I/O options:\n"
      << "  -i file   read input records from file and write sorted records to output\n"
      << "  -o file   output file for -i mode\n"
      << "  -S        flush mappings and call sync after sorting\n"
      << "  -v        verbose output\n"
      << "\n"
      << "Tuning options:\n"
      << "  -s ###    push-ahead stack size (default 16; 8 with -a)\n"
      << "  -c ###    group size at which to switch to cleanup sort"
         " (default 500)\n"
      << "  -t ###    cleanup size at which to use std::sort instead of shell sort"
         " (default 96; 256 with -a; 0 always uses std::sort)\n"
      << "  -A mode   output allocation: truncate or fallocate (default truncate)\n"
      << "  -I adv    input madvise: normal, random, sequential, willneed"
         " (default sequential)\n"
      << "  -O adv    output madvise: normal, random, sequential, willneed"
         " (default sequential)\n";
}

[[nodiscard]] Options parse_options(int argc, char* argv[]) {
  Options options;
  bool explicit_stack_size = false;
  bool explicit_cut_off = false;
  bool explicit_std_sort_threshold = false;

  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]) == "--help") {
      print_usage(argv[0], std::cout);
      std::exit(0);
    }
  }

  int opt = 0;
  while ((opt = ::getopt(argc, argv, "hSvi:o:aA:I:O:r:k:s:c:t:")) != -1) {
    switch (opt) {
      case 'h':
        print_usage(argv[0], std::cout);
        std::exit(0);
      case 'S':
        options.sync_after_sort = true;
        break;
      case 'v':
        options.verbose = true;
        break;
      case 'i':
        options.input_path = optarg;
        break;
      case 'o':
        options.output_path = optarg;
        break;
      case 'a':
        options.char_start = 32;
        options.char_stop = 126;
        break;
      case 'A':
        options.output_allocation = parse_allocation(optarg);
        break;
      case 'I':
        options.input_advice = parse_advice(optarg);
        break;
      case 'O':
        options.output_advice = parse_advice(optarg);
        break;
      case 'r':
        options.record_size = parse_positive_number(optarg, "record size");
        break;
      case 'k':
        options.key_size = parse_positive_number(optarg, "key size");
        break;
      case 's':
        options.stack_size = parse_positive_number(optarg, "stack size");
        explicit_stack_size = true;
        break;
      case 'c':
        options.cut_off = parse_positive_number(optarg, "cutoff");
        explicit_cut_off = true;
        break;
      case 't':
        options.std_sort_cleanup_min =
            parse_nonnegative_number(optarg, "std::sort threshold");
        explicit_std_sort_threshold = true;
        break;
      default:
        throw std::invalid_argument("invalid option");
    }
  }

  for (int index = optind; index < argc; ++index) {
    options.positional_paths.emplace_back(argv[index]);
  }

  if (options.key_size > options.record_size) {
    throw std::invalid_argument("key size must be less than or equal to record size");
  }

  apply_ascii_defaults(options,
                       explicit_stack_size,
                       explicit_cut_off,
                       explicit_std_sort_threshold);

  if (options.input_path.has_value()) {
    if (options.output_path.has_value()) {
      if (!options.positional_paths.empty()) {
        throw std::invalid_argument("-i with -o does not accept extra file arguments");
      }
    } else {
      if (options.positional_paths.size() != 1) {
        throw std::invalid_argument("-i mode requires an output file");
      }
      options.output_path = options.positional_paths.front();
    }
  } else {
    if (options.output_path.has_value()) {
      if (options.positional_paths.size() != 1) {
        throw std::invalid_argument("-o without -i requires exactly one input file");
      }
      options.input_path = options.positional_paths.front();
    } else if (options.positional_paths.size() != 1) {
      throw std::invalid_argument("expected exactly one file to sort");
    }
  }

  return options;
}
#endif

template <std::size_t FixedRecordSize>
class RecordScratch {
 public:
  explicit RecordScratch(std::size_t) {}

  [[nodiscard]] Byte* data() {
    return bytes_.data();
  }

 private:
  std::array<Byte, FixedRecordSize> bytes_ {};
};

template <>
class RecordScratch<0> {
 public:
  explicit RecordScratch(const std::size_t record_size)
      : bytes_(record_size) {}

  [[nodiscard]] Byte* data() {
    return bytes_.data();
  }

 private:
  std::vector<Byte> bytes_;
};

template <std::size_t FixedRecordSize,
          std::size_t FixedKeySize,
          std::uint16_t CharStart,
          std::uint16_t CharStop>
class RadixSorter {
  using AlphabetT = Alphabet<CharStart, CharStop>;
  using BucketMask = std::array<bool, AlphabetT::width>;
  using Histogram = typename AlphabetT::Histogram;
  using Histograms = typename AlphabetT::Histograms;

 public:
  explicit RadixSorter(const Options& options)
      : options_(options),
        record_scratch_(record_size()),
        position_stack_(options.stack_size + 1),
        next_histograms_(key_size()) {}

  void sort(const std::span<Byte> output,
            const std::optional<std::span<const Byte>> input) {
    sort(output, input, [] {});
  }

  template <typename InputConsumed>
  void sort(const std::span<Byte> output,
            const std::optional<std::span<const Byte>> input,
            InputConsumed input_consumed) {
    if (output.size() % record_size() != 0) {
      throw std::invalid_argument("file size is not a multiple of record size");
    }

    if (input.has_value() && input->size() != output.size()) {
      throw std::invalid_argument("input and output sizes differ");
    }

    const std::size_t record_count = output.size() / record_size();
    Histogram initial_histogram {};
    if (input.has_value()) {
      copy_input_and_count_first_digit(output, *input, initial_histogram);
      input_consumed();
    } else {
      validate_and_count_first_digit(output.data(),
                                     record_count,
                                     initial_histogram);
    }

    if (record_count <= 1) {
      return;
    }

    radixify(output.data(), record_count, 0, initial_histogram);
  }

 private:
  [[nodiscard]] std::size_t record_size() const {
    if constexpr (FixedRecordSize != 0) {
      return FixedRecordSize;
    } else {
      return options_.record_size;
    }
  }

  [[nodiscard]] std::size_t key_size() const {
    if constexpr (FixedKeySize != 0) {
      return FixedKeySize;
    } else {
      return options_.key_size;
    }
  }

  [[nodiscard]] Byte* record_at(Byte* const base,
                                const std::size_t record_index) const {
    return base + (record_index * record_size());
  }

  [[nodiscard]] const Byte* record_at(const Byte* const base,
                                      const std::size_t record_index) const {
    return base + (record_index * record_size());
  }

  [[nodiscard]] Byte* record_at_offset(Byte* const base,
                                       const std::size_t byte_offset) const {
    return base + byte_offset;
  }

  [[nodiscard]] const Byte* record_at_offset(
      const Byte* const base,
      const std::size_t byte_offset) const {
    return base + byte_offset;
  }

  void copy_record(Byte* const destination, const Byte* const source) const {
    if constexpr (FixedRecordSize == 100) {
#if defined(__GNUC__) || defined(__clang__)
      __builtin_memcpy(destination, source, 100);
#else
      std::memcpy(destination, source, 100);
#endif
    } else {
      std::memcpy(destination, source, record_size());
    }
  }

  [[nodiscard]] static std::uint64_t load_big_endian_u64(const Byte* const data) {
    std::uint64_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    if constexpr (std::endian::native == std::endian::little) {
      return std::byteswap(value);
    } else {
      return value;
    }
  }

  [[nodiscard]] static constexpr std::uint64_t repeated_byte(
      const std::uint8_t value) {
    return std::uint64_t {0x0101010101010101} * value;
  }

  [[nodiscard]] static bool has_zero_byte(const std::uint64_t value) {
    constexpr std::uint64_t ones = repeated_byte(0x01);
    constexpr std::uint64_t high_bits = repeated_byte(0x80);
    return ((value - ones) & ~value & high_bits) != 0;
  }

  [[nodiscard]] static bool has_byte_less_than(const std::uint64_t value,
                                               const std::uint8_t threshold) {
    const std::uint64_t thresholds = repeated_byte(threshold);
    constexpr std::uint64_t high_bits = repeated_byte(0x80);
    return ((value - thresholds) & ~value & high_bits) != 0;
  }

  [[nodiscard]] static bool printable_ascii_key8_is_valid(
      const Byte* const record) {
    constexpr std::uint64_t high_bits = repeated_byte(0x80);
    constexpr std::uint64_t delete_bytes = repeated_byte(0x7f);

    std::uint64_t key = 0;
    std::memcpy(&key, record, sizeof(key));

    return !has_byte_less_than(key, 32) && (key & high_bits) == 0 &&
           !has_zero_byte(key ^ delete_bytes);
  }

  [[nodiscard]] int compare_keys(const Byte* const left,
                                 const Byte* const right) const {
    if constexpr (FixedKeySize == 8) {
      const std::uint64_t left_key = load_big_endian_u64(left);
      const std::uint64_t right_key = load_big_endian_u64(right);
      return (left_key > right_key) - (left_key < right_key);
    } else {
      return std::memcmp(left, right, key_size());
    }
  }

  [[nodiscard]] bool key_less(const Byte* const left,
                              const Byte* const right) const {
    return compare_keys(left, right) < 0;
  }

  [[nodiscard]] std::uint8_t digit_at(const Byte* const base,
                                      const std::size_t record_index,
                                      const std::size_t digit) const {
    return to_uint8(record_at(base, record_index)[digit]);
  }

  [[nodiscard]] std::size_t digit_bin_at(const Byte* const base,
                                         const std::size_t record_index,
                                         const std::size_t digit) const {
    return AlphabetT::bin_for(record_at(base, record_index)[digit]);
  }

  [[nodiscard]] std::size_t digit_bin_at_offset(
      const Byte* const base,
      const std::size_t byte_offset,
      const std::size_t digit) const {
    return AlphabetT::bin_for(base[byte_offset + digit]);
  }

  [[nodiscard]] std::size_t validate_key_and_first_bin(
      const Byte* const record) const {
    if constexpr (FixedRecordSize == 100 && FixedKeySize == 8 &&
                  CharStart == 32 && CharStop == 126) {
      if (!printable_ascii_key8_is_valid(record)) {
        throw std::runtime_error(
            "record contains a key byte outside the configured alphabet");
      }
      return AlphabetT::bin_for(record[0]);
    } else if constexpr (AlphabetT::needs_validation) {
      std::size_t first_bin = 0;
      for (std::size_t digit = 0; digit < key_size(); ++digit) {
        const std::size_t bin = AlphabetT::checked_bin_for(record[digit]);
        if (digit == 0) {
          first_bin = bin;
        }
      }
      return first_bin;
    } else {
      return AlphabetT::bin_for(record[0]);
    }
  }

  [[nodiscard]] Histograms& next_histograms_for(
      const std::size_t digit,
      const BucketMask& rows_to_clear,
      const std::size_t rows_to_clear_count) {
    auto& histograms = next_histograms_[digit];
    if (!histograms) {
      histograms = std::make_unique_for_overwrite<Histograms>();
    }

    if (rows_to_clear_count == AlphabetT::width) {
      std::memset(histograms.get(), 0, sizeof(Histograms));
    } else {
      for (std::size_t bucket = 0; bucket < AlphabetT::width; ++bucket) {
        if (rows_to_clear[bucket]) {
          std::memset((*histograms)[bucket].data(), 0, sizeof(Histogram));
        }
      }
    }
    return *histograms;
  }

  void copy_input_and_count_first_digit(const std::span<Byte> output,
                                        const std::span<const Byte> input,
                                        Histogram& histogram) const {
    const std::size_t record_count = output.size() / record_size();
    for (std::size_t index = 0; index < record_count; ++index) {
      const Byte* const source = record_at(input.data(), index);
      Byte* const destination = record_at(output.data(), index);
      const std::size_t first_digit = validate_key_and_first_bin(source);
      ++histogram[first_digit];
      copy_record(destination, source);
    }
  }

  void validate_and_count_first_digit(const Byte* const buffer,
                                      const std::size_t count,
                                      Histogram& histogram) const {
    for (std::size_t index = 0; index < count; ++index) {
      ++histogram[validate_key_and_first_bin(record_at(buffer, index))];
    }
  }

  void shellsort(Byte* const buffer, const std::size_t count) {
    insertion_pass(buffer, count, 3);
    insertion_pass(buffer, count, 1);
  }

  void std_sort_cleanup(Byte* const buffer, const std::size_t count) {
    cleanup_order_.resize(count);
    const std::size_t record_bytes = record_size();
    std::size_t record_offset = 0;
    for (std::size_t& order_entry : cleanup_order_) {
      order_entry = record_offset;
      record_offset += record_bytes;
    }

    std::sort(cleanup_order_.begin(),
              cleanup_order_.end(),
              [this, buffer](const std::size_t left_offset,
                             const std::size_t right_offset) {
                return key_less(record_at_offset(buffer, left_offset),
                                record_at_offset(buffer, right_offset));
              });

    const std::size_t bytes = count * record_bytes;
    cleanup_buffer_.resize(bytes);
    Byte* destination = cleanup_buffer_.data();
    for (const std::size_t source_offset : cleanup_order_) {
      copy_record(destination, record_at_offset(buffer, source_offset));
      destination += record_bytes;
    }
    std::memcpy(buffer, cleanup_buffer_.data(), bytes);
  }

  void cleanup_sort(Byte* const buffer, const std::size_t count) {
    if constexpr (use_std_sort_cleanup) {
      if (count >= options_.std_sort_cleanup_min) {
        std_sort_cleanup(buffer, count);
      } else {
        shellsort(buffer, count);
      }
    } else {
      shellsort(buffer, count);
    }
  }

  void insertion_pass(Byte* const buffer,
                      const std::size_t count,
                      const std::size_t gap) {
    for (std::size_t index = gap; index < count; ++index) {
      copy_record(record_scratch_.data(), record_at(buffer, index));

      std::size_t current = index;
      while (current >= gap &&
             compare_keys(record_at(buffer, current - gap),
                          record_scratch_.data()) > 0) {
        copy_record(record_at(buffer, current),
                    record_at(buffer, current - gap));
        current -= gap;
      }

      copy_record(record_at(buffer, current), record_scratch_.data());
    }
  }

  template <bool CountNextDigit, bool UseRowMask>
  void permute_buckets(Byte* const buffer,
                       const std::size_t digit,
                       Histogram& offsets,
                       const Histogram& ends,
                       const std::size_t record_bytes,
                       const std::size_t stack_limit,
                       Histograms* const next_histogram,
                       const BucketMask& bucket_needs_next_histogram) {
    auto count_next_digit = [&](const std::size_t target_bucket,
                                const std::size_t source_offset) {
      if constexpr (CountNextDigit) {
        if constexpr (UseRowMask) {
          if (!bucket_needs_next_histogram[target_bucket]) {
            return;
          }
        }
        ++(*next_histogram)[target_bucket]
                           [digit_bin_at_offset(buffer,
                                                source_offset,
                                                digit + 1)];
      }
    };

    std::size_t* const position_stack = position_stack_.data();
    for (std::size_t bucket = 0; bucket < AlphabetT::width; ++bucket) {
      while (offsets[bucket] < ends[bucket]) {
        const std::size_t current_offset = offsets[bucket];
        if (digit_bin_at_offset(buffer, current_offset, digit) == bucket) {
          count_next_digit(bucket, current_offset);
          offsets[bucket] += record_bytes;
          continue;
        }

        std::size_t stack_pointer = 0;
        position_stack[stack_pointer++] = current_offset;

        std::size_t target =
            digit_bin_at_offset(buffer, current_offset, digit);

        while (target != bucket && stack_pointer <= stack_limit) {
          assert(offsets[target] < ends[target]);

          count_next_digit(target, position_stack[stack_pointer - 1]);

          position_stack[stack_pointer] = offsets[target];
          offsets[target] += record_bytes;

          target = digit_bin_at_offset(buffer,
                                       position_stack[stack_pointer],
                                       digit);
          ++stack_pointer;
        }

        if (target == bucket) {
          count_next_digit(target, position_stack[stack_pointer - 1]);
          offsets[bucket] += record_bytes;
        }

        --stack_pointer;
        copy_record(record_scratch_.data(),
                    record_at_offset(buffer, position_stack[stack_pointer]));

        while (stack_pointer != 0) {
          copy_record(record_at_offset(buffer, position_stack[stack_pointer]),
                      record_at_offset(buffer,
                                       position_stack[stack_pointer - 1]));
          --stack_pointer;
        }

        copy_record(record_at_offset(buffer, position_stack[0]),
                    record_scratch_.data());
      }
    }
  }

  void radixify(Byte* const buffer,
                const std::size_t count,
                const std::size_t digit,
                const Histogram& histogram) {
    if (count <= 1 || digit >= key_size()) {
      return;
    }

    if (count <= options_.cut_off) {
      cleanup_sort(buffer, count);
      return;
    }

    const std::size_t record_bytes = record_size();
    const std::size_t cut_off = options_.cut_off;
    const std::size_t stack_limit = options_.stack_size;
    const bool has_next_digit = digit + 1 < key_size();

    Histogram offsets {};
    Histogram starts {};
    Histogram ends {};
    BucketMask bucket_needs_next_histogram {};

    std::size_t records_seen = 0;
    std::size_t byte_offset = 0;
    std::size_t next_histogram_rows = 0;
    for (std::size_t bucket = 0; bucket < AlphabetT::width; ++bucket) {
      const std::size_t bucket_size = histogram[bucket];
      offsets[bucket] = byte_offset;
      starts[bucket] = byte_offset;
      byte_offset += bucket_size * record_bytes;
      ends[bucket] = byte_offset;
      records_seen += bucket_size;

      if (has_next_digit && bucket_size > cut_off) {
        bucket_needs_next_histogram[bucket] = true;
        ++next_histogram_rows;
      }
    }

    if (records_seen != count) {
      throw std::runtime_error("histogram does not match the record count");
    }

    Histograms* const next_histogram =
        next_histogram_rows != 0
            ? &next_histograms_for(digit,
                                    bucket_needs_next_histogram,
                                    next_histogram_rows)
            : nullptr;

    if (next_histogram == nullptr) {
      permute_buckets<false, false>(buffer,
                                    digit,
                                    offsets,
                                    ends,
                                    record_bytes,
                                    stack_limit,
                                    nullptr,
                                    bucket_needs_next_histogram);
    } else if (next_histogram_rows == AlphabetT::width) {
      permute_buckets<true, false>(buffer,
                                   digit,
                                   offsets,
                                   ends,
                                   record_bytes,
                                   stack_limit,
                                   next_histogram,
                                   bucket_needs_next_histogram);
    } else {
      permute_buckets<true, true>(buffer,
                                  digit,
                                  offsets,
                                  ends,
                                  record_bytes,
                                  stack_limit,
                                  next_histogram,
                                  bucket_needs_next_histogram);
    }

    if (!has_next_digit) {
      return;
    }

    for (std::size_t bucket_index = 0;
         bucket_index < AlphabetT::width;
         ++bucket_index) {
      const std::size_t bucket_size = histogram[bucket_index];
      if (bucket_size <= 1) {
        continue;
      }

      Byte* const bucket = record_at_offset(buffer, starts[bucket_index]);
      if (bucket_size > cut_off) {
        assert(next_histogram != nullptr);
        radixify(bucket, bucket_size, digit + 1, (*next_histogram)[bucket_index]);
      } else {
        cleanup_sort(bucket, bucket_size);
      }
    }
  }

  const Options& options_;
  RecordScratch<FixedRecordSize> record_scratch_;
  std::vector<std::size_t> position_stack_;
  std::vector<std::unique_ptr<Histograms>> next_histograms_;
  std::vector<std::size_t> cleanup_order_;
  std::vector<Byte> cleanup_buffer_;
};

[[nodiscard]] Options options_from_config(const bsort::SortConfig& config) {
  Options options;
  options.record_size = config.record_size;
  options.key_size = config.key_size;

  if (config.ascii) {
    options.char_start = 32;
    options.char_stop = 126;
  }

  const bool explicit_stack_size = config.stack_size != 0;
  const bool explicit_cut_off = config.cut_off != 0;
  const bool explicit_std_sort_threshold =
      config.std_sort_cleanup_min.has_value();

  if (explicit_stack_size) {
    options.stack_size = config.stack_size;
  }
  if (explicit_cut_off) {
    options.cut_off = config.cut_off;
  }
  if (explicit_std_sort_threshold) {
    options.std_sort_cleanup_min = *config.std_sort_cleanup_min;
  }

  if (options.record_size == 0) {
    throw std::invalid_argument("record size must be positive");
  }
  if (options.key_size == 0) {
    throw std::invalid_argument("key size must be positive");
  }
  if (options.key_size > options.record_size) {
    throw std::invalid_argument("key size must be less than or equal to record size");
  }

  apply_ascii_defaults(options,
                       explicit_stack_size,
                       explicit_cut_off,
                       explicit_std_sort_threshold);
  return options;
}

[[nodiscard]] bool ranges_overlap(const void* const left,
                                  const void* const right,
                                  const std::size_t byte_size) {
  if (byte_size == 0) {
    return false;
  }

  const auto left_begin = reinterpret_cast<std::uintptr_t>(left);
  const auto right_begin = reinterpret_cast<std::uintptr_t>(right);
  const std::uintptr_t max = ~std::uintptr_t {0};

  if (left_begin > max - byte_size || right_begin > max - byte_size) {
    throw std::invalid_argument("memory range size overflows the address space");
  }

  const std::uintptr_t left_end = left_begin + byte_size;
  const std::uintptr_t right_end = right_begin + byte_size;
  return left_begin < right_end && right_begin < left_end;
}

template <std::size_t FixedRecordSize,
          std::size_t FixedKeySize,
          std::uint16_t CharStart,
          std::uint16_t CharStop,
          typename InputConsumed>
void sort_memory_typed(const Options& options,
                       const std::span<Byte> output,
                       const std::optional<std::span<const Byte>> input,
                       InputConsumed input_consumed) {
  RadixSorter<FixedRecordSize, FixedKeySize, CharStart, CharStop> sorter(options);
  sorter.sort(output, input, input_consumed);
}

template <typename InputConsumed>
void sort_memory(const Options& options,
                 const std::span<Byte> output,
                 const std::optional<std::span<const Byte>> input,
                 InputConsumed input_consumed) {
  if (options.char_start == 32 && options.char_stop == 126) {
    if (options.record_size == 100 && options.key_size == 8) {
      sort_memory_typed<100, 8, 32, 126>(options,
                                         output,
                                         input,
                                         input_consumed);
      return;
    }

    sort_memory_typed<0, 0, 32, 126>(options, output, input, input_consumed);
    return;
  }

  if (options.record_size == 100 && options.key_size == 8) {
    sort_memory_typed<100, 8, 0, 255>(options, output, input, input_consumed);
    return;
  }

  sort_memory_typed<0, 0, 0, 255>(options, output, input, input_consumed);
}

#ifndef BSORT_LIBRARY_ONLY
[[nodiscard]] std::filesystem::path output_path_for(const Options& options) {
  if (options.output_path.has_value()) {
    return *options.output_path;
  }
  return options.positional_paths.front();
}

template <std::size_t FixedRecordSize,
          std::size_t FixedKeySize,
          std::uint16_t CharStart,
          std::uint16_t CharStop>
void run_sort_typed(const Options& options) {
  const auto start = std::chrono::steady_clock::now();

  if (options.input_path.has_value()) {
    const std::filesystem::path output_path = output_path_for(options);
    if (options.verbose) {
      std::cout << "sorting " << options.input_path->string() << " -> "
                << output_path.string() << '\n';
    }

    MappedFile input = MappedFile::open_existing(*options.input_path,
                                                 false,
                                                 options.input_advice);
    MappedFile output = MappedFile::create_sized(output_path,
                                                 input.size(),
                                                 options.output_allocation,
                                                 options.output_advice);
    sort_memory_typed<FixedRecordSize, FixedKeySize, CharStart, CharStop>(
        options,
        output.bytes(),
        input.bytes(),
        [&input] {
          input.close();
        });

    if (options.sync_after_sort) {
      output.flush_sync();
    }
  } else {
    const std::filesystem::path path = options.positional_paths.front();
    if (options.verbose) {
      std::cout << "sorting " << path.string() << '\n';
    }

    MappedFile file = MappedFile::open_existing(path,
                                                true,
                                                options.output_advice);
    sort_memory_typed<FixedRecordSize, FixedKeySize, CharStart, CharStop>(
        options,
        file.bytes(),
        std::nullopt,
        [] {});

    if (options.sync_after_sort) {
      file.flush_sync();
    }
  }

  const auto stop = std::chrono::steady_clock::now();
  const std::chrono::duration<double> elapsed = stop - start;
  std::cout << "Processing time: " << elapsed.count() << " s\n";
}

void run_sort(const Options& options) {
  if (options.char_start == 32 && options.char_stop == 126) {
    if (options.record_size == 100 && options.key_size == 8) {
      run_sort_typed<100, 8, 32, 126>(options);
      return;
    }

    run_sort_typed<0, 0, 32, 126>(options);
    return;
  }

  if (options.record_size == 100 && options.key_size == 8) {
    run_sort_typed<100, 8, 0, 255>(options);
    return;
  }

  run_sort_typed<0, 0, 0, 255>(options);
}
#endif

}  // namespace

namespace bsort {

void sort_records(void* const output,
                  const void* const input,
                  const std::size_t byte_size,
                  const SortConfig config) {
  Options options = options_from_config(config);

  if (byte_size != 0 && (output == nullptr || input == nullptr)) {
    throw std::invalid_argument("input and output pointers must be non-null");
  }

  const bool in_place = static_cast<const void*>(output) == input;
  if (!in_place && ranges_overlap(output, input, byte_size)) {
    throw std::invalid_argument("input and output memory ranges overlap");
  }

  Byte* const output_bytes = static_cast<Byte*>(output);
  std::span<Byte> output_span(output_bytes, byte_size);
  if (in_place) {
    sort_memory(options, output_span, std::nullopt, [] {});
    return;
  }

  const Byte* const input_bytes = static_cast<const Byte*>(input);
  sort_memory(options,
              output_span,
              std::span<const Byte>(input_bytes, byte_size),
              [] {});
}

void sort_records(char* const output,
                  const char* const input,
                  const std::size_t byte_size,
                  const SortConfig config) {
  sort_records(static_cast<void*>(output),
               static_cast<const void*>(input),
               byte_size,
               config);
}

}  // namespace bsort

#ifndef BSORT_LIBRARY_ONLY
int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    run_sort(options);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "bsort: " << error.what() << '\n';
    print_usage(argv[0], std::cerr);
    return 1;
  }
}
#endif

#include "predictfun/lifecycle/journal.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <span>
#include <type_traits>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace predictfun::lifecycle {
namespace {

constexpr std::array<char, 8U> file_magic{'P', 'F', 'J', 'R', 'N', 'L', '1', '\n'};
constexpr std::uint8_t record_version = 1U;

Error storage_error(std::string message, const std::filesystem::path &path) {
  return Error{ErrorCode::storage_failure, std::move(message), path.string()};
}

Error corrupt(std::string message, const std::filesystem::path &path) {
  return Error{ErrorCode::journal_corrupt, std::move(message), path.string()};
}

template <class Integer>
void append_le(std::vector<std::uint8_t> &out, Integer value) {
  using Unsigned = std::make_unsigned_t<Integer>;
  auto bits = static_cast<Unsigned>(value);
  for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
    out.push_back(static_cast<std::uint8_t>(bits & 0xffU));
    bits >>= 8U;
  }
}

template <class Integer>
bool read_le(std::span<const std::uint8_t> input, std::size_t &offset,
             Integer &value) {
  if (offset > input.size() || input.size() - offset < sizeof(Integer))
    return false;
  using Unsigned = std::make_unsigned_t<Integer>;
  Unsigned bits = 0U;
  for (std::size_t index = 0U; index < sizeof(Integer); ++index)
    bits |= static_cast<Unsigned>(input[offset++]) << (index * 8U);
  value = static_cast<Integer>(bits);
  return true;
}

bool append_string(std::vector<std::uint8_t> &out, std::string_view value) {
  if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
  append_le(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
  return true;
}

bool read_string(std::span<const std::uint8_t> input, std::size_t &offset,
                 std::string &value) {
  std::uint32_t size = 0U;
  if (!read_le(input, offset, size) || offset > input.size() ||
      size > input.size() - offset)
    return false;
  value.assign(reinterpret_cast<const char *>(input.data() + offset), size);
  offset += size;
  return true;
}

std::uint32_t crc32(std::span<const std::uint8_t> input) {
  std::uint32_t crc = 0xffffffffU;
  for (const auto byte : input) {
    crc ^= byte;
    for (unsigned bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^
            (0xedb88320U &
             (0U - static_cast<std::uint32_t>(crc & 1U)));
  }
  return ~crc;
}

Result<std::vector<std::uint8_t>> encode(const TrackedOrder &order,
                                         std::size_t maximum) {
  std::vector<std::uint8_t> payload;
  payload.reserve(256U);
  payload.push_back(record_version);
  payload.push_back(static_cast<std::uint8_t>(order.state));
  payload.push_back(order.reconciliation_required ? 1U : 0U);
  payload.push_back(order.order_id ? 1U : 0U);
  append_le(payload, order.stream_generation);
  append_le(payload, order.last_event_timestamp_ms);
  if (!append_string(payload, order.order_hash) ||
      !append_string(payload, order.order_id.value_or("")) ||
      !append_string(payload, order.amount.to_string()) ||
      !append_string(payload, order.amount_filled.to_string()) ||
      !append_string(payload, order.reason))
    return Error{ErrorCode::numeric_overflow,
                 "lifecycle journal field exceeds uint32 length", {}};
  if (payload.size() > maximum)
    return Error{ErrorCode::body_too_large,
                 "lifecycle journal record exceeds configured bound", {}};
  return payload;
}

Result<TrackedOrder> decode(std::span<const std::uint8_t> payload) {
  std::size_t offset = 0U;
  if (payload.size() < 4U || payload[offset++] != record_version)
    return Error{ErrorCode::journal_corrupt,
                 "unsupported lifecycle journal record version", {}};
  const auto state_raw = payload[offset++];
  if (state_raw > static_cast<std::uint8_t>(OrderLifecycleState::unknown))
    return Error{ErrorCode::journal_corrupt,
                 "invalid lifecycle state in journal", {}};
  const bool reconciliation_required = payload[offset++] != 0U;
  const bool has_order_id = payload[offset++] != 0U;
  std::uint64_t generation = 0U;
  std::int64_t timestamp = 0;
  std::string hash;
  std::string order_id;
  std::string amount;
  std::string amount_filled;
  std::string reason;
  if (!read_le(payload, offset, generation) ||
      !read_le(payload, offset, timestamp) ||
      !read_string(payload, offset, hash) ||
      !read_string(payload, offset, order_id) ||
      !read_string(payload, offset, amount) ||
      !read_string(payload, offset, amount_filled) ||
      !read_string(payload, offset, reason) || offset != payload.size())
    return Error{ErrorCode::journal_corrupt,
                 "malformed lifecycle journal record", {}};
  auto parsed_amount = ExactDecimal::parse(amount);
  auto parsed_filled = ExactDecimal::parse(amount_filled);
  if (!parsed_amount || !parsed_filled)
    return Error{ErrorCode::journal_corrupt,
                 "invalid decimal in lifecycle journal", {}};
  return TrackedOrder{
      std::move(hash),
      has_order_id ? std::optional<std::string>{std::move(order_id)}
                   : std::nullopt,
      std::move(parsed_amount.value()), std::move(parsed_filled.value()),
      static_cast<OrderLifecycleState>(state_raw), reconciliation_required,
      generation, timestamp, std::move(reason)};
}

bool write_all(std::ofstream &stream, std::span<const std::uint8_t> bytes) {
  stream.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return stream.good();
}

Result<bool> sync_file_to_stable_storage(const std::filesystem::path &path) {
#if defined(_WIN32)
  const auto handle = CreateFileW(
      path.c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return storage_error("cannot open lifecycle journal for durable sync",
                         path);
  const auto flushed = FlushFileBuffers(handle) != 0;
  const auto closed = CloseHandle(handle) != 0;
  if (!flushed || !closed)
    return storage_error("cannot durably sync lifecycle journal", path);
#else
  const auto descriptor = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (descriptor < 0)
    return storage_error("cannot open lifecycle journal for durable sync",
                         path);
  const auto flushed = ::fsync(descriptor) == 0;
  const auto closed = ::close(descriptor) == 0;
  if (!flushed || !closed)
    return storage_error("cannot durably sync lifecycle journal", path);
#endif
  return true;
}

} // namespace

OrderJournal::OrderJournal(std::filesystem::path path, JournalOptions options)
    : path_(std::move(path)), options_(options) {}

Result<bool> OrderJournal::initialize() {
  if (path_.empty())
    return Error{ErrorCode::invalid_argument,
                 "lifecycle journal path must not be empty", "path"};
  if (options_.maximum_record_bytes < 128U)
    return Error{ErrorCode::invalid_argument,
                 "maximum journal record size is too small",
                 "maximum_record_bytes"};
  std::error_code error;
  if (const auto parent = path_.parent_path(); !parent.empty())
    std::filesystem::create_directories(parent, error);
  if (error) return storage_error("cannot create journal directory", path_);

  if (std::filesystem::exists(path_, error)) {
    if (error) return storage_error("cannot inspect lifecycle journal", path_);
    return true;
  }
  std::ofstream stream(path_, std::ios::binary | std::ios::out);
  if (!stream) return storage_error("cannot create lifecycle journal", path_);
  stream.write(file_magic.data(), static_cast<std::streamsize>(file_magic.size()));
  stream.flush();
  if (!stream.good())
    return storage_error("cannot initialize lifecycle journal", path_);
  if (options_.flush_after_append) {
    auto synced = sync_file_to_stable_storage(path_);
    if (!synced) return synced.error();
  }
  return true;
}

Result<bool> OrderJournal::append(const TrackedOrder &order) {
  return append(std::vector<TrackedOrder>{order});
}

Result<bool> OrderJournal::append(const std::vector<TrackedOrder> &orders) {
  auto initialized = initialize();
  if (!initialized) return initialized.error();
  std::ofstream stream(path_, std::ios::binary | std::ios::app);
  if (!stream) return storage_error("cannot open lifecycle journal", path_);
  for (const auto &order : orders) {
    auto encoded = encode(order, options_.maximum_record_bytes);
    if (!encoded) return encoded.error();
    std::vector<std::uint8_t> header;
    header.reserve(8U);
    append_le(header,
              static_cast<std::uint32_t>(encoded.value().size()));
    append_le(header, crc32(encoded.value()));
    if (!write_all(stream, header) || !write_all(stream, encoded.value()))
      return storage_error("cannot append lifecycle journal record", path_);
  }
  if (options_.flush_after_append) stream.flush();
  if (!stream.good())
    return storage_error("cannot flush lifecycle journal", path_);
  if (options_.flush_after_append) {
    stream.close();
    if (stream.fail())
      return storage_error("cannot close lifecycle journal", path_);
    auto synced = sync_file_to_stable_storage(path_);
    if (!synced) return synced.error();
  }
  return true;
}

Result<JournalReplay> OrderJournal::replay() const {
  std::ifstream stream(path_, std::ios::binary);
  if (!stream) return storage_error("cannot open lifecycle journal", path_);
  std::array<char, file_magic.size()> magic{};
  stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (stream.gcount() != static_cast<std::streamsize>(magic.size()) ||
      magic != file_magic)
    return corrupt("invalid lifecycle journal header", path_);

  JournalReplay replay;
  replay.valid_bytes = file_magic.size();
  std::unordered_map<std::string, TrackedOrder> latest;
  for (;;) {
    std::array<std::uint8_t, 8U> header{};
    stream.read(reinterpret_cast<char *>(header.data()),
                static_cast<std::streamsize>(header.size()));
    const auto header_read = stream.gcount();
    if (header_read == 0) break;
    if (header_read != static_cast<std::streamsize>(header.size())) {
      replay.ignored_truncated_tail = true;
      break;
    }
    std::size_t offset = 0U;
    std::uint32_t size = 0U;
    std::uint32_t expected_crc = 0U;
    if (!read_le(std::span<const std::uint8_t>{header}, offset, size) ||
        !read_le(std::span<const std::uint8_t>{header}, offset, expected_crc))
      return corrupt("malformed lifecycle journal frame", path_);
    if (size > options_.maximum_record_bytes)
      return corrupt("lifecycle journal frame exceeds configured bound", path_);
    std::vector<std::uint8_t> payload(size);
    stream.read(reinterpret_cast<char *>(payload.data()),
                static_cast<std::streamsize>(payload.size()));
    if (stream.gcount() != static_cast<std::streamsize>(payload.size())) {
      replay.ignored_truncated_tail = true;
      break;
    }
    if (crc32(payload) != expected_crc)
      return corrupt("lifecycle journal checksum mismatch", path_);
    auto decoded = decode(payload);
    if (!decoded) return corrupt(decoded.error().message, path_);
    latest.insert_or_assign(decoded.value().order_hash,
                            std::move(decoded.value()));
    ++replay.records_applied;
    replay.valid_bytes += header.size() + payload.size();
  }
  replay.orders.reserve(latest.size());
  for (auto &[hash, order] : latest) {
    (void)hash;
    replay.orders.push_back(std::move(order));
  }
  std::ranges::sort(replay.orders, {}, &TrackedOrder::order_hash);
  return replay;
}

Result<PersistentOrderTracker>
PersistentOrderTracker::open(std::filesystem::path path,
                             JournalOptions options) {
  OrderJournal journal(std::move(path), options);
  auto initialized = journal.initialize();
  if (!initialized) return initialized.error();
  auto replay = journal.replay();
  if (!replay) return replay.error();
  if (replay.value().ignored_truncated_tail) {
    std::error_code error;
    std::filesystem::resize_file(journal.path(), replay.value().valid_bytes,
                                 error);
    if (error)
      return storage_error("cannot repair truncated lifecycle journal",
                           journal.path());
  }
  OrderTracker tracker;
  auto restored = tracker.restore(replay.value().orders);
  if (!restored) return restored.error();
  std::uint64_t generation = 1U;
  for (const auto &order : replay.value().orders)
    generation = std::max(generation, order.stream_generation + 1U);
  tracker.require_reconciliation(generation);
  auto quarantined = tracker.snapshot();
  if (!quarantined.empty()) {
    auto persisted = journal.append(quarantined);
    if (!persisted) return persisted.error();
  }
  return PersistentOrderTracker{
      std::move(journal), std::move(tracker), replay.value().records_applied,
      replay.value().ignored_truncated_tail};
}

Result<TrackedOrder *>
PersistentOrderTracker::begin_submission(std::string hash,
                                         ExactDecimal amount) {
  auto next = tracker_;
  auto changed = next.begin_submission(hash, std::move(amount));
  if (!changed) return changed.error();
  auto persisted = journal_.append(*changed.value());
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return tracker_.find(hash);
}

Result<bool> PersistentOrderTracker::apply_create_outcome(
    std::string_view hash,
    const MutationOutcome<CreateOrderReceipt> &outcome) {
  auto next = tracker_;
  auto changed = next.apply_create_outcome(hash, outcome);
  if (!changed) return changed.error();
  auto persisted = journal_.append(*next.find(hash));
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return true;
}

Result<bool>
PersistentOrderTracker::apply_rest_order(const OrderRecord &order) {
  auto next = tracker_;
  auto changed = next.apply_rest_order(order);
  if (!changed) return changed.error();
  auto persisted = journal_.append(*next.find(order.order.hash));
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return true;
}

Result<bool>
PersistentOrderTracker::apply_wallet_event(const WalletEvent &event) {
  auto next = tracker_;
  auto changed = next.apply_wallet_event(event);
  if (!changed) return changed.error();
  auto persisted = journal_.append(*next.find(event.order_hash));
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return true;
}

Result<bool>
PersistentOrderTracker::mark_book_removed(std::string_view hash) {
  auto next = tracker_;
  auto changed = next.mark_book_removed(hash);
  if (!changed) return changed.error();
  auto persisted = journal_.append(*next.find(hash));
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return true;
}

Result<bool> PersistentOrderTracker::require_reconciliation(
    std::uint64_t stream_generation) {
  auto next = tracker_;
  next.require_reconciliation(stream_generation);
  auto persisted = journal_.append(next.snapshot());
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return true;
}

Result<ReconciliationReport> PersistentOrderTracker::reconcile(
    std::uint64_t stream_generation,
    const std::vector<OrderRecord> &complete_order_snapshot) {
  auto next = tracker_;
  auto report = next.reconcile(stream_generation, complete_order_snapshot);
  auto persisted = journal_.append(next.snapshot());
  if (!persisted) return persisted.error();
  tracker_ = std::move(next);
  return report;
}

} // namespace predictfun::lifecycle

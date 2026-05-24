#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "tweenacl.h"

namespace zetha::crypt {

// --- Error Handling ---
// Pattern follows ZethaStorage-style Expected/Error conventions
// similar to Errc/Error/Expected in ZethaSTORAGE.hpp:54-82 and
// structured error/result handling in ZethaRDB.hpp:133-173.

enum class CryptErrc {
    Ok = 0,
    InvalidArgument,
    KeySizeMismatch,
    NonceSizeMismatch,
    EncryptFailed,
    DecryptFailed,
    AuthenticationFailed,
    CorruptRecord,
    NotFound,
    IoError,
    InternalError
};

struct Error {
    CryptErrc code{CryptErrc::Ok};
    std::string message{};

    constexpr explicit operator bool() const noexcept {
        return code != CryptErrc::Ok;
    }
};

template <class T>
struct ExpectedType {
    using type = std::variant<T, Error>;
};

template <>
struct ExpectedType<void> {
    using type = std::optional<Error>;
};

template <class T>
using Expected = typename ExpectedType<T>::type;

// --- Secure zeroization helper ---

inline void secure_memzero(void* ptr, std::size_t n) noexcept {
    if (!ptr || n == 0) {
        return;
    }
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (n--) {
        *p++ = 0;
    }
}

// --- Secure Buffer ---
// Similar in spirit to Zetha binary buffers using std::vector<std::byte>
// as seen in ZethaSTORAGE.hpp PageBuffer and byte-oriented storage
// helpers around ZethaSTORAGE.hpp:248-254, 407, 731, 909.

class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(std::size_t n) : data_(n) {}

    ~SecureBuffer() {
        cleanse();
    }

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    SecureBuffer(SecureBuffer&& other) noexcept
        : data_(std::move(other.data_)) {
        other.scrub_storage_only();
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
        if (this != &other) {
            cleanse();
            data_ = std::move(other.data_);
            other.scrub_storage_only();
        }
        return *this;
    }

    [[nodiscard]] std::byte* data() noexcept { return data_.data(); }
    [[nodiscard]] const std::byte* data() const noexcept { return data_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    [[nodiscard]] std::vector<std::byte>& vec() noexcept { return data_; }
    [[nodiscard]] const std::vector<std::byte>& vec() const noexcept { return data_; }

    void resize(std::size_t n) { data_.resize(n); }
    void clear() noexcept { cleanse(); }

    void cleanse() noexcept {
        scrub_storage_only();
        data_.clear();
        data_.shrink_to_fit();
    }

private:
    void scrub_storage_only() noexcept {
        if (!data_.empty()) {
            secure_memzero(data_.data(), data_.size());
        }
    }

    std::vector<std::byte> data_;
};

// --- Cryptographic Constants ---

constexpr std::size_t CRYPT_KEY_BYTES     = crypto_secretbox_KEYBYTES;
constexpr std::size_t CRYPT_NONCE_BYTES   = crypto_secretbox_NONCEBYTES;
constexpr std::size_t CRYPT_ZEROBYTES     = crypto_secretbox_ZEROBYTES;
constexpr std::size_t CRYPT_BOXZEROBYTES  = crypto_secretbox_BOXZEROBYTES;

// --- Binary On-Disk Structures ---
// Style inspired by packed POD storage structs in ZethaSTORAGE.hpp,
// e.g. FileHeaderFixed with static_assert validation.

#pragma pack(push, 1)

struct CryptFileHeader {
    std::uint32_t magic = 0x5A435259u; // "ZCRY"
    std::uint16_t version = 1;
    std::uint16_t flags = 0;
    std::array<std::byte, 16> salt{};
    std::array<std::byte, 40> reserved{};
};
static_assert(std::is_trivially_copyable_v<CryptFileHeader>);
static_assert(sizeof(CryptFileHeader) == 64);

struct CryptRecordHeader {
    std::uint32_t key_size = 0;
    std::uint32_t value_size_plaintext = 0;
    std::uint32_t value_size_ciphertext = 0;
    std::array<std::byte, CRYPT_NONCE_BYTES> nonce{};
    std::array<std::byte, 16> key_id{};
};
static_assert(std::is_trivially_copyable_v<CryptRecordHeader>);
static_assert(sizeof(CryptRecordHeader) == (sizeof(std::uint32_t) * 3 + CRYPT_NONCE_BYTES + 16));

#pragma pack(pop)

// --- High-level encrypted value ---

struct EncryptedValue {
    std::array<std::byte, CRYPT_NONCE_BYTES> nonce{};
    std::vector<std::byte> cipher{};
    std::uint32_t plain_size = 0;
};

// --- Serialized record helper ---

struct SerializedRecord {
    CryptRecordHeader header{};
    std::vector<std::byte> key{};
    std::vector<std::byte> cipher{};
};

// --- Internal helpers ---

namespace detail {

inline void random_fill(void* dst, std::size_t n) {
    randombytes_buf(dst, static_cast<unsigned long long>(n));
}

[[nodiscard]] inline auto bytes_to_uchar(std::byte* p) noexcept -> unsigned char* {
    return reinterpret_cast<unsigned char*>(p);
}

[[nodiscard]] inline auto bytes_to_uchar(const std::byte* p) noexcept -> const unsigned char* {
    return reinterpret_cast<const unsigned char*>(p);
}

template <class T>
[[nodiscard]] inline auto holds_error(const Expected<T>& v) -> bool {
    return std::holds_alternative<Error>(v);
}

template <class T>
[[nodiscard]] inline auto get_error(Expected<T>& v) -> Error& {
    return std::get<Error>(v);
}

template <class T>
[[nodiscard]] inline auto get_error(const Expected<T>& v) -> const Error& {
    return std::get<Error>(v);
}

template <class T>
[[nodiscard]] inline auto get_value(Expected<T>& v) -> T& {
    return std::get<T>(v);
}

template <class T>
[[nodiscard]] inline auto get_value(const Expected<T>& v) -> const T& {
    return std::get<T>(v);
}

[[nodiscard]] inline auto make_error(CryptErrc code, std::string msg) -> Error {
    return Error{code, std::move(msg)};
}

[[nodiscard]] inline auto checked_cipher_size_from_plain(std::size_t plain_size) -> std::optional<std::size_t> {
    if (plain_size > (static_cast<std::size_t>(-1) - CRYPT_ZEROBYTES + CRYPT_BOXZEROBYTES)) {
        return std::nullopt;
    }
    return plain_size + CRYPT_ZEROBYTES - CRYPT_BOXZEROBYTES;
}

[[nodiscard]] inline auto checked_padded_plain_size(std::size_t plain_size) -> std::optional<std::size_t> {
    if (plain_size > (static_cast<std::size_t>(-1) - CRYPT_ZEROBYTES)) {
        return std::nullopt;
    }
    return plain_size + CRYPT_ZEROBYTES;
}

[[nodiscard]] inline auto checked_padded_cipher_size(std::size_t cipher_size) -> std::optional<std::size_t> {
    if (cipher_size > (static_cast<std::size_t>(-1) - CRYPT_BOXZEROBYTES)) {
        return std::nullopt;
    }
    return cipher_size + CRYPT_BOXZEROBYTES;
}

} // namespace detail

// --- Core cryptographic operations ---

class ZethaCRYPT {
public:
    using ByteVec = std::vector<std::byte>;

    [[nodiscard]] static auto generate_key() -> SecureBuffer {
        SecureBuffer key(CRYPT_KEY_BYTES);
        detail::random_fill(key.data(), key.size());
        return key;
    }

    [[nodiscard]] static auto generate_nonce() -> std::array<std::byte, CRYPT_NONCE_BYTES> {
        std::array<std::byte, CRYPT_NONCE_BYTES> nonce{};
        detail::random_fill(nonce.data(), nonce.size());
        return nonce;
    }

    [[nodiscard]] static auto encrypt(
        std::span<const std::byte> plaintext,
        std::span<const std::byte> key
    ) -> Expected<EncryptedValue> {
        if (key.size() != CRYPT_KEY_BYTES) {
            return detail::make_error(
                CryptErrc::KeySizeMismatch,
                "Invalid key size provided for encryption."
            );
        }

        const auto padded_plaintext_len_opt = detail::checked_padded_plain_size(plaintext.size());
        const auto ciphertext_len_opt = detail::checked_cipher_size_from_plain(plaintext.size());

        if (!padded_plaintext_len_opt || !ciphertext_len_opt) {
            return detail::make_error(
                CryptErrc::InvalidArgument,
                "Plaintext size is too large."
            );
        }

        const std::size_t padded_plaintext_len = *padded_plaintext_len_opt;
        const std::size_t ciphertext_len = *ciphertext_len_opt;

        std::array<std::byte, CRYPT_NONCE_BYTES> nonce = generate_nonce();

        SecureBuffer padded_plaintext(padded_plaintext_len);
        std::memset(padded_plaintext.data(), 0, padded_plaintext_len);
        if (!plaintext.empty()) {
            std::memcpy(
                padded_plaintext.data() + CRYPT_ZEROBYTES,
                plaintext.data(),
                plaintext.size()
            );
        }

        ByteVec padded_ciphertext(padded_plaintext_len);
        std::memset(padded_ciphertext.data(), 0, padded_ciphertext.size());

        const int rc = crypto_secretbox(
            detail::bytes_to_uchar(padded_ciphertext.data()),
            detail::bytes_to_uchar(padded_plaintext.data()),
            static_cast<unsigned long long>(padded_plaintext_len),
            detail::bytes_to_uchar(nonce.data()),
            detail::bytes_to_uchar(key.data())
        );

        if (rc != 0) {
            return detail::make_error(
                CryptErrc::EncryptFailed,
                "crypto_secretbox encryption failed."
            );
        }

        EncryptedValue out;
        out.nonce = nonce;
        out.plain_size = static_cast<std::uint32_t>(plaintext.size());
        out.cipher.resize(ciphertext_len);

        if (ciphertext_len != 0) {
            std::memcpy(
                out.cipher.data(),
                padded_ciphertext.data() + CRYPT_BOXZEROBYTES,
                ciphertext_len
            );
        }

        secure_memzero(padded_ciphertext.data(), padded_ciphertext.size());
        return out;
    }

    [[nodiscard]] static auto decrypt(
        const EncryptedValue& encrypted_value,
        std::span<const std::byte> key
    ) -> Expected<ByteVec> {
        if (key.size() != CRYPT_KEY_BYTES) {
            return detail::make_error(
                CryptErrc::KeySizeMismatch,
                "Invalid key size provided for decryption."
            );
        }

        if (encrypted_value.cipher.empty()) {
            if (encrypted_value.plain_size == 0) {
                return ByteVec{};
            }
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Ciphertext is empty but plaintext size is non-zero."
            );
        }

        const auto expected_cipher_len_opt =
            detail::checked_cipher_size_from_plain(encrypted_value.plain_size);
        const auto padded_input_len_opt =
            detail::checked_padded_cipher_size(encrypted_value.cipher.size());

        if (!expected_cipher_len_opt || !padded_input_len_opt) {
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Record size overflow while validating ciphertext."
            );
        }

        const std::size_t expected_cipher_len = *expected_cipher_len_opt;
        const std::size_t padded_input_len = *padded_input_len_opt;

        if (encrypted_value.cipher.size() != expected_cipher_len) {
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Ciphertext size does not match expected size based on plaintext size."
            );
        }

        ByteVec padded_input(padded_input_len);
        std::memset(padded_input.data(), 0, padded_input.size());
        std::memcpy(
            padded_input.data() + CRYPT_BOXZEROBYTES,
            encrypted_value.cipher.data(),
            encrypted_value.cipher.size()
        );

        SecureBuffer padded_output(padded_input_len);
        std::memset(padded_output.data(), 0, padded_output.size());

        const int rc = crypto_secretbox_open(
            detail::bytes_to_uchar(padded_output.data()),
            detail::bytes_to_uchar(padded_input.data()),
            static_cast<unsigned long long>(padded_input_len),
            detail::bytes_to_uchar(encrypted_value.nonce.data()),
            detail::bytes_to_uchar(key.data())
        );

        secure_memzero(padded_input.data(), padded_input.size());

        if (rc != 0) {
            return detail::make_error(
                CryptErrc::AuthenticationFailed,
                "crypto_secretbox_open failed: authentication failed or ciphertext is corrupt."
            );
        }

        ByteVec plaintext(encrypted_value.plain_size);
        if (encrypted_value.plain_size != 0) {
            std::memcpy(
                plaintext.data(),
                padded_output.data() + CRYPT_ZEROBYTES,
                encrypted_value.plain_size
            );
        }

        return plaintext;
    }

    [[nodiscard]] static auto serialize_record(
        std::span<const std::byte> key,
        const EncryptedValue& value
    ) -> Expected<ByteVec> {
        if (key.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return detail::make_error(
                CryptErrc::InvalidArgument,
                "Key size exceeds 32-bit record format limit."
            );
        }

        if (value.cipher.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return detail::make_error(
                CryptErrc::InvalidArgument,
                "Ciphertext size exceeds 32-bit record format limit."
            );
        }

        CryptRecordHeader header{};
        header.key_size = static_cast<std::uint32_t>(key.size());
        header.value_size_plaintext = value.plain_size;
        header.value_size_ciphertext = static_cast<std::uint32_t>(value.cipher.size());
        header.nonce = value.nonce;
        header.key_id = make_key_id(key);

        const std::size_t total_size =
            sizeof(CryptRecordHeader) + key.size() + value.cipher.size();

        ByteVec out(total_size);
        std::byte* p = out.data();

        std::memcpy(p, &header, sizeof(header));
        p += sizeof(header);

        if (!key.empty()) {
            std::memcpy(p, key.data(), key.size());
            p += key.size();
        }

        if (!value.cipher.empty()) {
            std::memcpy(p, value.cipher.data(), value.cipher.size());
        }

        return out;
    }

    [[nodiscard]] static auto deserialize_record(
        std::span<const std::byte> bytes
    ) -> Expected<SerializedRecord> {
        if (bytes.size() < sizeof(CryptRecordHeader)) {
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Buffer too small to contain CryptRecordHeader."
            );
        }

        SerializedRecord record{};
        std::memcpy(&record.header, bytes.data(), sizeof(CryptRecordHeader));

        const std::size_t expected_size =
            sizeof(CryptRecordHeader) +
            static_cast<std::size_t>(record.header.key_size) +
            static_cast<std::size_t>(record.header.value_size_ciphertext);

        if (bytes.size() != expected_size) {
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Serialized record size does not match header fields."
            );
        }

        const auto expected_cipher_len_opt =
            detail::checked_cipher_size_from_plain(record.header.value_size_plaintext);

        if (!expected_cipher_len_opt) {
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Plaintext size causes ciphertext size overflow."
            );
        }

        if (record.header.value_size_ciphertext != *expected_cipher_len_opt &&
            !(record.header.value_size_plaintext == 0 && record.header.value_size_ciphertext == 0)) {
            return detail::make_error(
                CryptErrc::CorruptRecord,
                "Header plaintext/ciphertext sizes are inconsistent."
            );
        }

        const std::byte* p = bytes.data() + sizeof(CryptRecordHeader);

        record.key.resize(record.header.key_size);
        if (record.header.key_size != 0) {
            std::memcpy(record.key.data(), p, record.key.size());
            p += record.key.size();
        }

        record.cipher.resize(record.header.value_size_ciphertext);
        if (record.header.value_size_ciphertext != 0) {
            std::memcpy(record.cipher.data(), p, record.cipher.size());
        }

        return record;
    }

    [[nodiscard]] static auto encrypt_record(
        std::span<const std::byte> key,
        std::span<const std::byte> plaintext,
        std::span<const std::byte> master_key
    ) -> Expected<ByteVec> {
        auto enc = encrypt(plaintext, master_key);
        if (detail::holds_error(enc)) {
            return detail::get_error(enc);
        }
        return serialize_record(key, detail::get_value(enc));
    }

    [[nodiscard]] static auto decrypt_record(
        std::span<const std::byte> serialized_record,
        std::span<const std::byte> expected_key,
        std::span<const std::byte> master_key
    ) -> Expected<ByteVec> {
        auto parsed = deserialize_record(serialized_record);
        if (detail::holds_error(parsed)) {
            return detail::get_error(parsed);
        }

        const auto& rec = detail::get_value(parsed);

        if (rec.key.size() != expected_key.size()) {
            return detail::make_error(
                CryptErrc::NotFound,
                "Record key does not match requested key."
            );
        }

        if (!rec.key.empty() &&
            std::memcmp(rec.key.data(), expected_key.data(), rec.key.size()) != 0) {
            return detail::make_error(
                CryptErrc::NotFound,
                "Record key does not match requested key."
            );
        }

        EncryptedValue ev{};
        ev.nonce = rec.header.nonce;
        ev.cipher = rec.cipher;
        ev.plain_size = rec.header.value_size_plaintext;

        return decrypt(ev, master_key);
    }

    [[nodiscard]] static auto make_key_id(
        std::span<const std::byte> key
    ) -> std::array<std::byte, 16> {
        std::array<std::byte, 16> out{};
        if (key.empty()) {
            return out;
        }

        for (std::size_t i = 0; i < out.size(); ++i) {
            const std::byte b = key[i % key.size()];
            const std::byte mix = std::byte{static_cast<unsigned char>((i * 17u + 31u) & 0xFFu)};
            out[i] = std::byte{
                static_cast<unsigned char>(
                    static_cast<unsigned char>(b) ^
                    static_cast<unsigned char>(mix)
                )
            };
        }
        return out;
    }
};

// --- Minimal in-memory encrypted KV store ---
// This is a simple utility wrapper. Persistence can be layered on top
// using Zetha storage facilities.

class ZethaCryptKV {
public:
    using ByteVec = std::vector<std::byte>;

    explicit ZethaCryptKV(SecureBuffer master_key)
        : master_key_(std::move(master_key)) {}

    explicit ZethaCryptKV(std::span<const std::byte> master_key)
        : master_key_(master_key.size()) {
        if (!master_key.empty()) {
            std::memcpy(master_key_.data(), master_key.data(), master_key.size());
        }
    }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return master_key_.size() == CRYPT_KEY_BYTES;
    }

    [[nodiscard]] auto put(
        std::string_view key,
        std::span<const std::byte> value
    ) -> Expected<void> {
        if (!valid()) {
            return Error{
                CryptErrc::KeySizeMismatch,
                "Master key size is invalid."
            };
        }

        const auto key_bytes = as_bytes(key);

        auto enc = ZethaCRYPT::encrypt(value, std::span<const std::byte>(master_key_.data(), master_key_.size()));
        if (detail::holds_error(enc)) {
            return detail::get_error(enc);
        }

        Entry entry;
        entry.key.assign(key_bytes.begin(), key_bytes.end());
        entry.value = detail::get_value(enc);

        const auto idx = find_entry(key);
        if (idx < entries_.size()) {
            entries_[idx] = std::move(entry);
        } else {
            entries_.push_back(std::move(entry));
        }

        return std::nullopt;
    }

    [[nodiscard]] auto get(
        std::string_view key
    ) const -> Expected<ByteVec> {
        if (!valid()) {
            return Error{
                CryptErrc::KeySizeMismatch,
                "Master key size is invalid."
            };
        }

        const auto idx = find_entry(key);
        if (idx >= entries_.size()) {
            return Error{
                CryptErrc::NotFound,
                "Key not found."
            };
        }

        return ZethaCRYPT::decrypt(
            entries_[idx].value,
            std::span<const std::byte>(master_key_.data(), master_key_.size())
        );
    }

    [[nodiscard]] auto erase(
        std::string_view key
    ) -> Expected<void> {
        const auto idx = find_entry(key);
        if (idx >= entries_.size()) {
            return Error{
                CryptErrc::NotFound,
                "Key not found."
            };
        }

        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(idx));
        return std::nullopt;
    }

    [[nodiscard]] auto contains(std::string_view key) const -> bool {
        return find_entry(key) < entries_.size();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return entries_.size();
    }

    void clear() noexcept {
        entries_.clear();
    }

private:
    struct Entry {
        ByteVec key{};
        EncryptedValue value{};
    };

    [[nodiscard]] static auto as_bytes(std::string_view s) -> std::span<const std::byte> {
        return {
            reinterpret_cast<const std::byte*>(s.data()),
            s.size()
        };
    }

    [[nodiscard]] auto find_entry(std::string_view key) const -> std::size_t {
        const auto key_bytes = as_bytes(key);
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].key.size() != key_bytes.size()) {
                continue;
            }
            if (entries_[i].key.empty()) {
                return i;
            }
            if (std::memcmp(entries_[i].key.data(), key_bytes.data(), key_bytes.size()) == 0) {
                return i;
            }
        }
        return entries_.size();
    }

    SecureBuffer master_key_;
    std::vector<Entry> entries_;
};

} // namespace zetha::crypt

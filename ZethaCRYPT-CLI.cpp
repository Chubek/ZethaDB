#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "tweenacl.h"

// This header is designed to be stylistically consistent with the
// existing Zetha* CLI layers (ZethaMEM-CLI.cpp, ZethaINDEX-CLI.cpp,
// ZethaRDB-CLI.cpp) and the core headers (ZethaSTORAGE.hpp, etc.).
//
// It provides a small CLI-facing API around tweenacl-based symmetric
// encryption (crypto_secretbox_xsalsa20poly1305).

namespace zetha::crypt::cli {

// ------------------------ Error handling ------------------------

enum class Errc {
    Ok = 0,
    InvalidArgument,
    KeySizeMismatch,
    NonceSizeMismatch,
    EncryptFailed,
    DecryptFailed,
    AuthenticationFailed,
    IoError,
    InternalError
};

struct Error {
    Errc        code { Errc::Ok };
    std::string message;

    constexpr explicit operator bool() const noexcept {
        return code != Errc::Ok;
    }
};

template<class T>
using Expected = std::variant<T, Error>;

using ExpectedVoid = std::optional<Error>;

inline Error make_error(Errc c, std::string msg) {
    return Error{c, std::move(msg)};
}

template<class T>
inline bool is_error(const Expected<T> &e) {
    return std::holds_alternative<Error>(e);
}

template<class T>
inline Error &get_error(Expected<T> &e) {
    return std::get<Error>(e);
}

template<class T>
inline const Error &get_error(const Expected<T> &e) {
    return std::get<Error>(e);
}

template<class T>
inline T &get_value(Expected<T> &e) {
    return std::get<T>(e);
}

template<class T>
inline const T &get_value(const Expected<T> &e) {
    return std::get<T>(e);
}

// ------------------------ Secure buffer ------------------------

// Very small helper for CLI key material; mirrors the “secure zeroize”
// patterns used in other Zetha components when handling sensitive data.

class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(std::size_t n) : buf_(n) {}

    ~SecureBuffer() { cleanse(); }

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    SecureBuffer(SecureBuffer&& other) noexcept : buf_(std::move(other.buf_)) {
        other.scrub_only();
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
        if (this != &other) {
            cleanse();
            buf_ = std::move(other.buf_);
            other.scrub_only();
        }
        return *this;
    }

    std::byte*       data()       noexcept { return buf_.data(); }
    const std::byte* data() const noexcept { return buf_.data(); }
    std::size_t      size() const noexcept { return buf_.size(); }

    void resize(std::size_t n) { buf_.resize(n); }

    void cleanse() noexcept {
        scrub_only();
        buf_.clear();
        buf_.shrink_to_fit();
    }

private:
    void scrub_only() noexcept {
        if (!buf_.empty()) {
            volatile unsigned char* p =
                reinterpret_cast<volatile unsigned char*>(buf_.data());
            for (std::size_t i = 0; i < buf_.size(); ++i) {
                p[i] = 0;
            }
        }
    }

    std::vector<std::byte> buf_;
};

// ------------------------ Crypto constants ------------------------

constexpr std::size_t KEY_BYTES    = crypto_secretbox_KEYBYTES;
constexpr std::size_t NONCE_BYTES  = crypto_secretbox_NONCEBYTES;
constexpr std::size_t ZEROBYTES    = crypto_secretbox_ZEROBYTES;
constexpr std::size_t BOXZEROBYTES = crypto_secretbox_BOXZEROBYTES;

// ------------------------ Data types ------------------------

struct EncryptedBlob {
    // Nonce used for this blob
    std::vector<std::byte> nonce;
    // Ciphertext (without leading BOXZEROBYTES)
    std::vector<std::byte> cipher;
};

struct HexKey {
    // For CLI convenience we often treat keys as hex strings.
    std::string hex;
};

struct BinaryKey {
    // Raw bytes; must be KEY_BYTES in length when used with crypto_secretbox.
    std::vector<std::byte> data;
};

// ------------------------ Helpers ------------------------

inline void random_fill(void* dst, std::size_t n) {
    if (n == 0) return;
    randombytes_buf(dst, static_cast<unsigned long long>(n));
}

inline unsigned char* as_uchar(std::byte* p) {
    return reinterpret_cast<unsigned char*>(p);
}

inline const unsigned char* as_uchar(const std::byte* p) {
    return reinterpret_cast<const unsigned char*>(p);
}

inline bool hex_to_bytes(std::string_view hex, std::vector<std::byte>& dst) {
    dst.clear();

    if (hex.size() % 2 != 0) {
        return false;
    }

    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    dst.reserve(hex.size() / 2);

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            dst.clear();
            return false;
        }
        unsigned char v = static_cast<unsigned char>((hi << 4) | lo);
        dst.push_back(static_cast<std::byte>(v));
    }
    return true;
}

inline std::string bytes_to_hex(const std::byte* data, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(n * 2);

    for (std::size_t i = 0; i < n; ++i) {
        unsigned char v = static_cast<unsigned char>(data[i]);
        out[2 * i]     = digits[(v >> 4) & 0x0F];
        out[2 * i + 1] = digits[v & 0x0F];
    }
    return out;
}

// ------------------------ Core API ------------------------
//
// These functions are intended to be used by CLI front-ends, analogous
// to how the other *-CLI.cpp files call into their respective modules.

// Generate a new random symmetric key suitable for crypto_secretbox.
inline Expected<BinaryKey> generate_key() {
    BinaryKey k;
    k.data.resize(KEY_BYTES);
    random_fill(k.data.data(), k.data.size());
    return k;
}

// Same as generate_key(), but returns hex for CLI printing.
inline Expected<HexKey> generate_key_hex() {
    auto ek = generate_key();
    if (is_error(ek)) return get_error(ek);

    const auto& k = get_value(ek);
    HexKey hk;
    hk.hex = bytes_to_hex(k.data.data(), k.data.size());
    return hk;
}

// Parse a hex-encoded key (e.g., from CLI input).
inline Expected<BinaryKey> parse_hex_key(std::string_view hex) {
    BinaryKey k;
    if (!hex_to_bytes(hex, k.data)) {
        return make_error(Errc::InvalidArgument, "invalid hex key");
    }
    if (k.data.size() != KEY_BYTES) {
        return make_error(Errc::KeySizeMismatch, "key must be 32 bytes (64 hex chars)");
    }
    return k;
}

// Encrypt arbitrary binary data with a given key.
// Plaintext is provided as pointer + size pair for CLI flexibility.
inline Expected<EncryptedBlob> encrypt(
    const BinaryKey& key,
    const void*      data,
    std::size_t      size
) {
    if (key.data.size() != KEY_BYTES) {
        return make_error(Errc::KeySizeMismatch, "invalid key size");
    }

    // crypto_secretbox requires ZEROBYTES leading zeros on plaintext
    // and returns BOXZEROBYTES leading zeros in ciphertext.
    std::size_t padded_plain_len = size + ZEROBYTES;
    std::size_t padded_cipher_len = padded_plain_len;

    std::vector<std::byte> padded_plain(padded_plain_len);
    std::vector<std::byte> padded_cipher(padded_cipher_len);

    // Zero leading ZEROBYTES
    std::memset(padded_plain.data(), 0, ZEROBYTES);
    if (size > 0) {
        std::memcpy(padded_plain.data() + ZEROBYTES, data, size);
    }

    // Generate nonce
    std::vector<std::byte> nonce(NONCE_BYTES);
    random_fill(nonce.data(), nonce.size());

    int rc = crypto_secretbox(
        as_uchar(padded_cipher.data()),
        as_uchar(padded_plain.data()),
        static_cast<unsigned long long>(padded_plain_len),
        as_uchar(nonce.data()),
        as_uchar(key.data.data())
    );

    // wipe plaintext buffer
    {
        volatile unsigned char* p =
            reinterpret_cast<volatile unsigned char*>(padded_plain.data());
        for (std::size_t i = 0; i < padded_plain.size(); ++i) {
            p[i] = 0;
        }
    }

    if (rc != 0) {
        return make_error(Errc::EncryptFailed, "crypto_secretbox failed");
    }

    EncryptedBlob blob;
    blob.nonce = std::move(nonce);

    // Drop leading BOXZEROBYTES
    if (padded_cipher_len <= BOXZEROBYTES) {
        blob.cipher.clear();
    } else {
        std::size_t cipher_len = padded_cipher_len - BOXZEROBYTES;
        blob.cipher.resize(cipher_len);
        std::memcpy(
            blob.cipher.data(),
            padded_cipher.data() + BOXZEROBYTES,
            cipher_len
        );
    }

    // wipe padded_cipher
    {
        volatile unsigned char* p =
            reinterpret_cast<volatile unsigned char*>(padded_cipher.data());
        for (std::size_t i = 0; i < padded_cipher.size(); ++i) {
            p[i] = 0;
        }
    }

    return blob;
}

// Decrypt a blob produced by encrypt() above.
inline Expected<std::vector<std::byte>> decrypt(
    const BinaryKey&   key,
    const EncryptedBlob& blob
) {
    if (key.data.size() != KEY_BYTES) {
        return make_error(Errc::KeySizeMismatch, "invalid key size");
    }
    if (blob.nonce.size() != NONCE_BYTES) {
        return make_error(Errc::NonceSizeMismatch, "invalid nonce size");
    }

    // Reconstruct padded cipher: BOXZEROBYTES zeros + stored cipher
    std::size_t padded_cipher_len = BOXZEROBYTES + blob.cipher.size();
    std::vector<std::byte> padded_cipher(padded_cipher_len);
    std::memset(padded_cipher.data(), 0, BOXZEROBYTES);

    if (!blob.cipher.empty()) {
        std::memcpy(
            padded_cipher.data() + BOXZEROBYTES,
            blob.cipher.data(),
            blob.cipher.size()
        );
    }

    std::vector<std::byte> padded_plain(padded_cipher_len);

    int rc = crypto_secretbox_open(
        as_uchar(padded_plain.data()),
        as_uchar(padded_cipher.data()),
        static_cast<unsigned long long>(padded_cipher_len),
        as_uchar(blob.nonce.data()),
        as_uchar(key.data.data())
    );

    // wipe padded_cipher
    {
        volatile unsigned char* p =
            reinterpret_cast<volatile unsigned char*>(padded_cipher.data());
        for (std::size_t i = 0; i < padded_cipher.size(); ++i) {
            p[i] = 0;
        }
    }

    if (rc != 0) {
        // wipe padded_plain before returning
        {
            volatile unsigned char* p =
                reinterpret_cast<volatile unsigned char*>(padded_plain.data());
            for (std::size_t i = 0; i < padded_plain.size(); ++i) {
                p[i] = 0;
            }
        }
        return make_error(
            Errc::AuthenticationFailed,
            "crypto_secretbox_open failed (auth failure or corrupt data)"
        );
    }

    // Drop ZEROBYTES leading zeros to recover original plaintext
    std::vector<std::byte> plain;
    if (padded_plain.size() > ZEROBYTES) {
        std::size_t plain_len = padded_plain.size() - ZEROBYTES;
        plain.resize(plain_len);
        std::memcpy(
            plain.data(),
            padded_plain.data() + ZEROBYTES,
            plain_len
        );
    }

    // wipe padded_plain
    {
        volatile unsigned char* p =
            reinterpret_cast<volatile unsigned char*>(padded_plain.data());
        for (std::size_t i = 0; i < padded_plain.size(); ++i) {
            p[i] = 0;
        }
    }

    return plain;
}

// ------------------------ CLI-oriented helpers ------------------------
//
// These helpers provide hex-friendly wrappers for use in an actual
// ZethaCRYPT-CLI.cpp implementation.  The CLI file can:
//
//   - read a key as hex from stdin / argv
//   - call parse_hex_key()
//   - call encrypt_hex / decrypt_hex()
//   - print hex outputs.

inline Expected<EncryptedBlob> encrypt_buffer_with_hex_key(
    std::string_view hex_key,
    const void*      data,
    std::size_t      size
) {
    auto k = parse_hex_key(hex_key);
    if (is_error(k)) return get_error(k);
    return encrypt(get_value(k), data, size);
}

inline Expected<std::vector<std::byte>> decrypt_buffer_with_hex_key(
    std::string_view hex_key,
    const EncryptedBlob& blob
) {
    auto k = parse_hex_key(hex_key);
    if (is_error(k)) return get_error(k);
    return decrypt(get_value(k), blob);
}

// Serialize EncryptedBlob (nonce + cipher) to hex for CLI output.
inline std::string blob_to_hex(const EncryptedBlob& b) {
    std::string out;
    out.reserve((b.nonce.size() + b.cipher.size()) * 2);
    out += bytes_to_hex(b.nonce.data(), b.nonce.size());
    out += bytes_to_hex(b.cipher.data(), b.cipher.size());
    return out;
}

// Parse EncryptedBlob from hex (first NONCE_BYTES, then cipher).
inline Expected<EncryptedBlob> hex_to_blob(std::string_view hex) {
    std::vector<std::byte> bytes;
    if (!hex_to_bytes(hex, bytes)) {
        return make_error(Errc::InvalidArgument, "invalid hex blob");
    }
    if (bytes.size() < NONCE_BYTES) {
        return make_error(Errc::InvalidArgument, "blob too short for nonce");
    }

    EncryptedBlob b;
    b.nonce.assign(bytes.begin(), bytes.begin() + NONCE_BYTES);
    b.cipher.assign(bytes.begin() + NONCE_BYTES, bytes.end());
    return b;
}

} // namespace zetha::crypt::cli

#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t kMaximumWeatherResponseBytes = 4096;

enum class BoundedReaderFinish : uint8_t {
  kComplete,
  kLimitExceeded,
};

// ArduinoJson accepts any reader that implements read() and readBytes(). This
// adapter keeps parsing on the source stream while making the decoded body
// limit independently testable on a host.
template <typename TSource>
class BoundedStreamReader {
 public:
  BoundedStreamReader(TSource& source, size_t byte_limit)
      : source_(source), remaining_(byte_limit), bytes_read_(0) {}

  int read() {
    if (remaining_ == 0) {
      return -1;
    }

    char value = 0;
    if (source_.readBytes(&value, 1) != 1) {
      return -1;
    }

    --remaining_;
    ++bytes_read_;
    return static_cast<unsigned char>(value);
  }

  size_t readBytes(char* buffer, size_t length) {
    if (length > remaining_) {
      length = remaining_;
    }
    if (length == 0) {
      return 0;
    }

    size_t bytes_read = source_.readBytes(buffer, length);
    if (bytes_read > length) {
      bytes_read = length;
    }
    remaining_ -= bytes_read;
    bytes_read_ += bytes_read;
    return bytes_read;
  }

  // ArduinoJson stops after the JSON value. For an unknown-length response,
  // consume the remaining decoded body without buffering it, then probe one
  // byte beyond the allowance. This distinguishes an exact-limit body from an
  // oversized one.
  BoundedReaderFinish finish() {
    while (remaining_ > 0) {
      if (read() < 0) {
        return BoundedReaderFinish::kComplete;
      }
    }

    char overflow_byte = 0;
    return source_.readBytes(&overflow_byte, 1) == 1
               ? BoundedReaderFinish::kLimitExceeded
               : BoundedReaderFinish::kComplete;
  }

  size_t bytes_read() const { return bytes_read_; }

 private:
  TSource& source_;
  size_t remaining_;
  size_t bytes_read_;
};

// HTTPClient::getStreamPtr() exposes chunk framing in ESP32 Arduino core 3.0.7.
// Decode that framing lazily so ArduinoJson can remain a direct stream parser.
template <typename TSource>
class ChunkedBodyReader {
 public:
  explicit ChunkedBodyReader(TSource& source)
      : source_(source),
        chunk_remaining_(0),
        needs_chunk_terminator_(false),
        complete_(false),
        failed_(false) {}

  int read() {
    char value = 0;
    return readBytes(&value, 1) == 1 ? static_cast<unsigned char>(value) : -1;
  }

  size_t readBytes(char* buffer, size_t length) {
    size_t total_read = 0;
    while (total_read < length && !complete_ && !failed_) {
      if (chunk_remaining_ == 0 && !prepare_next_chunk()) {
        break;
      }

      size_t requested = length - total_read;
      if (requested > chunk_remaining_) {
        requested = chunk_remaining_;
      }

      const size_t bytes_read =
          source_.readBytes(buffer + total_read, requested);
      if (bytes_read > requested) {
        failed_ = true;
        break;
      }

      total_read += bytes_read;
      chunk_remaining_ -= bytes_read;
      if (bytes_read != requested) {
        failed_ = true;
        break;
      }
      if (chunk_remaining_ == 0) {
        needs_chunk_terminator_ = true;
      }
    }
    return total_read;
  }

  bool complete() const { return complete_; }
  bool failed() const { return failed_; }

 private:
  static constexpr size_t kMaximumLineBytes = 128;
  static constexpr size_t kMaximumTrailerBytes = 512;

  bool read_raw_byte(char* value) {
    if (source_.readBytes(value, 1) == 1) {
      return true;
    }
    failed_ = true;
    return false;
  }

  bool read_line(char* line, size_t capacity, size_t* length) {
    *length = 0;
    while (true) {
      char value = 0;
      if (!read_raw_byte(&value)) {
        return false;
      }
      if (value == '\r') {
        char newline = 0;
        if (!read_raw_byte(&newline) || newline != '\n') {
          failed_ = true;
          return false;
        }
        line[*length] = '\0';
        return true;
      }
      if (value == '\n' || *length + 1 >= capacity) {
        failed_ = true;
        return false;
      }
      line[(*length)++] = value;
    }
  }

  bool parse_chunk_size(const char* line, size_t length, size_t* chunk_size) {
    size_t index = 0;
    size_t value = 0;
    bool has_digit = false;
    while (index < length && line[index] != ';') {
      const char digit = line[index++];
      uint8_t decoded = 0;
      if (digit >= '0' && digit <= '9') {
        decoded = static_cast<uint8_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        decoded = static_cast<uint8_t>(digit - 'a' + 10);
      } else if (digit >= 'A' && digit <= 'F') {
        decoded = static_cast<uint8_t>(digit - 'A' + 10);
      } else {
        return false;
      }
      has_digit = true;
      if (value > (SIZE_MAX - decoded) / 16U) {
        return false;
      }
      value = value * 16U + decoded;
    }

    if (!has_digit) {
      return false;
    }
    *chunk_size = value;
    return true;
  }

  bool consume_trailers() {
    size_t total_trailer_bytes = 0;
    while (true) {
      char line[kMaximumLineBytes] = {};
      size_t length = 0;
      if (!read_line(line, sizeof(line), &length)) {
        return false;
      }
      total_trailer_bytes += length + 2;
      if (total_trailer_bytes > kMaximumTrailerBytes) {
        failed_ = true;
        return false;
      }
      if (length == 0) {
        return true;
      }
    }
  }

  bool prepare_next_chunk() {
    if (needs_chunk_terminator_) {
      char carriage_return = 0;
      char newline = 0;
      if (!read_raw_byte(&carriage_return) || !read_raw_byte(&newline) ||
          carriage_return != '\r' || newline != '\n') {
        failed_ = true;
        return false;
      }
      needs_chunk_terminator_ = false;
    }

    char line[kMaximumLineBytes] = {};
    size_t length = 0;
    if (!read_line(line, sizeof(line), &length)) {
      return false;
    }

    size_t next_chunk_size = 0;
    if (!parse_chunk_size(line, length, &next_chunk_size)) {
      failed_ = true;
      return false;
    }
    if (next_chunk_size == 0) {
      if (!consume_trailers()) {
        return false;
      }
      complete_ = true;
      return false;
    }

    chunk_remaining_ = next_chunk_size;
    return true;
  }

  TSource& source_;
  size_t chunk_remaining_;
  bool needs_chunk_terminator_;
  bool complete_;
  bool failed_;
};

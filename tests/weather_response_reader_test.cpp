#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <string>

#include "weather_response_reader.h"

namespace {
class FakeStream {
 public:
  explicit FakeStream(const std::string& data) : data_(data), offset_(0) {}

  size_t readBytes(char* buffer, size_t length) {
    const size_t available = data_.size() - offset_;
    if (length > available) {
      length = available;
    }
    if (length > 0) {
      memcpy(buffer, data_.data() + offset_, length);
      offset_ += length;
    }
    return length;
  }

  size_t offset() const { return offset_; }

 private:
  std::string data_;
  size_t offset_;
};

void test_bounded_read_methods_do_not_cross_limit() {
  FakeStream source("abcdef");
  BoundedStreamReader<FakeStream> reader(source, 4);

  char first[3] = {};
  assert(reader.readBytes(first, sizeof(first)) == sizeof(first));
  assert(memcmp(first, "abc", sizeof(first)) == 0);
  assert(reader.read() == 'd');
  assert(reader.read() == -1);
  assert(reader.bytes_read() == 4);
  assert(source.offset() == 4);
}

void test_finish_accepts_short_and_exact_bodies() {
  FakeStream short_source("abc");
  BoundedStreamReader<FakeStream> short_reader(short_source, 4);
  assert(short_reader.read() == 'a');
  assert(short_reader.finish() == BoundedReaderFinish::kComplete);
  assert(short_reader.bytes_read() == 3);

  FakeStream exact_source(std::string(kMaximumWeatherResponseBytes, 'x'));
  BoundedStreamReader<FakeStream> exact_reader(exact_source,
                                                kMaximumWeatherResponseBytes);
  assert(exact_reader.finish() == BoundedReaderFinish::kComplete);
  assert(exact_reader.bytes_read() == kMaximumWeatherResponseBytes);
  assert(exact_source.offset() == kMaximumWeatherResponseBytes);
}

void test_finish_detects_first_overflow_byte() {
  FakeStream source(std::string(kMaximumWeatherResponseBytes + 1, 'x'));
  BoundedStreamReader<FakeStream> reader(source,
                                          kMaximumWeatherResponseBytes);

  assert(reader.finish() == BoundedReaderFinish::kLimitExceeded);
  assert(reader.bytes_read() == kMaximumWeatherResponseBytes);
  assert(source.offset() == kMaximumWeatherResponseBytes + 1);
}

void test_chunked_body_decodes_across_chunk_boundaries() {
  FakeStream source("3\r\nabc\r\n2;ext=value\r\nde\r\n0\r\nHeader: value\r\n\r\n");
  ChunkedBodyReader<FakeStream> decoded(source);
  BoundedStreamReader<ChunkedBodyReader<FakeStream>> reader(decoded, 5);

  char body[6] = {};
  assert(reader.readBytes(body, sizeof(body)) == 5);
  assert(strcmp(body, "abcde") == 0);
  assert(reader.finish() == BoundedReaderFinish::kComplete);
  assert(decoded.complete());
  assert(!decoded.failed());
}

void test_chunked_body_still_enforces_decoded_limit() {
  FakeStream source("6\r\nabcdef\r\n0\r\n\r\n");
  ChunkedBodyReader<FakeStream> decoded(source);
  BoundedStreamReader<ChunkedBodyReader<FakeStream>> reader(decoded, 5);

  assert(reader.finish() == BoundedReaderFinish::kLimitExceeded);
  assert(reader.bytes_read() == 5);
}

void test_invalid_chunk_framing_fails_closed() {
  FakeStream source("3\r\nabcX\n0\r\n\r\n");
  ChunkedBodyReader<FakeStream> decoded(source);
  char body[4] = {};

  assert(decoded.readBytes(body, 3) == 3);
  assert(decoded.read() == -1);
  assert(decoded.failed());
  assert(!decoded.complete());
}
}  // namespace

int main() {
  test_bounded_read_methods_do_not_cross_limit();
  test_finish_accepts_short_and_exact_bodies();
  test_finish_detects_first_overflow_byte();
  test_chunked_body_decodes_across_chunk_boundaries();
  test_chunked_body_still_enforces_decoded_limit();
  test_invalid_chunk_framing_fails_closed();
  return 0;
}

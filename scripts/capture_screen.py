#!/usr/bin/env python3
"""Capture Project Jarvis's LVGL framebuffer over a raw USB serial tty."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import select
import struct
import sys
import termios
import time
import tty
import zlib


SCREENSHOT_PREFIX = b"SCREENSHOT_BEGIN"
SCREENSHOT_ERROR_PREFIX = b"SCREENSHOT_ERROR"
TILE_ERROR_PREFIX = b"tile_error="
TILE_SELECTED_PREFIX = b"tile_selected="
SCREENSHOT_FORMAT = "rgb565le"
BYTES_PER_PIXEL = 2
HEADER_TIMEOUT_SECONDS = 15.0
TRANSFER_TIMEOUT_SECONDS = 30.0
TILE_RESPONSE_TIMEOUT_SECONDS = 5.0
TILE_REFRESH_DELAY_SECONDS = 0.1
MAX_HEADER_BYTES = 4096
MIN_TILE_INDEX = 0
MAX_TILE_INDEX = 5


class CaptureError(Exception):
    """An expected serial, protocol, or output failure."""


class SerialReader:
    def __init__(self, file_descriptor: int) -> None:
        self.file_descriptor = file_descriptor
        self.buffer = bytearray()

    def _read_more(self, deadline: float, context: str) -> None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CaptureError(f"timed out while {context}")

        readable, _, _ = select.select([self.file_descriptor], [], [], remaining)
        if not readable:
            raise CaptureError(f"timed out while {context}")

        chunk = os.read(self.file_descriptor, 4096)
        if not chunk:
            raise CaptureError(f"serial device closed while {context}")
        self.buffer.extend(chunk)

    def read_line(self, timeout: float, context: str) -> bytes:
        deadline = time.monotonic() + timeout
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self.buffer[: newline + 1])
                del self.buffer[: newline + 1]
                return line
            if len(self.buffer) > MAX_HEADER_BYTES:
                raise CaptureError(f"line too long while {context}")
            self._read_more(deadline, context)

    def read_exact(self, byte_count: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        while len(self.buffer) < byte_count:
            self._read_more(
                deadline,
                f"reading framebuffer ({len(self.buffer)}/{byte_count} bytes)",
            )
        result = bytes(self.buffer[:byte_count])
        del self.buffer[:byte_count]
        return result


def parse_screenshot_header(line: bytes) -> tuple[int, int, int] | None:
    stripped = line.rstrip(b"\r\n")
    if not stripped.startswith(SCREENSHOT_PREFIX):
        return None

    try:
        text = stripped.decode("ascii")
    except UnicodeDecodeError as error:
        raise CaptureError("malformed SCREENSHOT_BEGIN header: non-ASCII data") from error

    tokens = text.split()
    if not tokens or tokens[0] != SCREENSHOT_PREFIX.decode("ascii"):
        raise CaptureError("malformed SCREENSHOT_BEGIN header")

    fields: dict[str, str] = {}
    for token in tokens[1:]:
        if "=" not in token:
            raise CaptureError(f"malformed SCREENSHOT_BEGIN field: {token!r}")
        key, value = token.split("=", 1)
        if not key or not value or key in fields:
            raise CaptureError(f"malformed SCREENSHOT_BEGIN field: {token!r}")
        fields[key] = value

    required = {"width", "height", "format", "bytes"}
    missing = sorted(required - fields.keys())
    if missing:
        raise CaptureError(
            "malformed SCREENSHOT_BEGIN header: missing " + ", ".join(missing)
        )

    try:
        width = int(fields["width"], 10)
        height = int(fields["height"], 10)
        byte_count = int(fields["bytes"], 10)
    except ValueError as error:
        raise CaptureError("malformed SCREENSHOT_BEGIN numeric field") from error

    if width <= 0 or height <= 0:
        raise CaptureError("malformed SCREENSHOT_BEGIN dimensions")
    if fields["format"] != SCREENSHOT_FORMAT:
        raise CaptureError(f"unsupported screenshot format: {fields['format']}")
    expected_bytes = width * height * BYTES_PER_PIXEL
    if byte_count != expected_bytes:
        raise CaptureError(
            f"invalid byte count: declared {byte_count}, expected {expected_bytes}"
        )
    return width, height, byte_count


def read_screenshot(reader: SerialReader) -> tuple[int, int, bytes]:
    while True:
        line = reader.read_line(HEADER_TIMEOUT_SECONDS, "waiting for screenshot header")
        stripped = line.rstrip(b"\r\n")
        if stripped.startswith(SCREENSHOT_ERROR_PREFIX):
            message = stripped.decode("ascii", errors="replace")
            raise CaptureError(f"device rejected screenshot: {message}")

        parsed = parse_screenshot_header(line)
        if parsed is not None:
            width, height, byte_count = parsed
            break

    pixels = reader.read_exact(byte_count, TRANSFER_TIMEOUT_SECONDS)

    separator = reader.read_line(TRANSFER_TIMEOUT_SECONDS, "reading screenshot marker")
    if separator not in (b"\n", b"\r\n"):
        raise CaptureError("malformed screenshot transfer: missing marker separator")

    marker = reader.read_line(TRANSFER_TIMEOUT_SECONDS, "reading screenshot marker")
    if marker.rstrip(b"\r\n") != b"SCREENSHOT_END":
        raise CaptureError("malformed screenshot transfer: missing SCREENSHOT_END")
    return width, height, pixels


def validate_tile_index(tile: int) -> int:
    if tile < MIN_TILE_INDEX or tile > MAX_TILE_INDEX:
        raise CaptureError(
            f"tile index must be between {MIN_TILE_INDEX} and {MAX_TILE_INDEX}"
        )
    return tile


def parse_tile_argument(value: str) -> int:
    try:
        tile = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("tile index must be an integer from 0 to 5") from error
    try:
        return validate_tile_index(tile)
    except CaptureError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def tile_command(tile: int) -> bytes:
    return f"tile {validate_tile_index(tile)}\n".encode("ascii")


def parse_tile_response(line: bytes, requested_tile: int) -> bool | None:
    stripped = line.rstrip(b"\r\n")
    expected = f"tile_selected={validate_tile_index(requested_tile)}".encode("ascii")
    if stripped == expected:
        return True
    if stripped.startswith(TILE_SELECTED_PREFIX):
        response = stripped.decode("ascii", errors="replace")
        raise CaptureError(f"device selected an unexpected tile: {response}")
    if stripped.startswith(TILE_ERROR_PREFIX):
        response = stripped.decode("ascii", errors="replace")
        raise CaptureError(f"device rejected tile selection: {response}")
    return None


def wait_for_tile_selection(reader: SerialReader, requested_tile: int) -> None:
    deadline = time.monotonic() + TILE_RESPONSE_TIMEOUT_SECONDS
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CaptureError("timed out waiting for tile selection")
        line = reader.read_line(remaining, "waiting for tile selection")
        if parse_tile_response(line, requested_tile):
            return


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    checksum = zlib.crc32(chunk_type)
    checksum = zlib.crc32(data, checksum) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", checksum)


def encode_rgb565le_png(width: int, height: int, pixels: bytes) -> bytes:
    expected_bytes = width * height * BYTES_PER_PIXEL
    if width <= 0 or height <= 0 or len(pixels) != expected_bytes:
        raise CaptureError(
            f"invalid RGB565 image: {width}x{height} requires {expected_bytes} bytes, "
            f"received {len(pixels)}"
        )

    scanlines = bytearray(height * (1 + width * 3))
    input_offset = 0
    output_offset = 0
    for _ in range(height):
        scanlines[output_offset] = 0
        output_offset += 1
        for _ in range(width):
            value = pixels[input_offset] | (pixels[input_offset + 1] << 8)
            input_offset += 2
            red = (value >> 11) & 0x1F
            green = (value >> 5) & 0x3F
            blue = value & 0x1F
            scanlines[output_offset] = (red * 255 + 15) // 31
            scanlines[output_offset + 1] = (green * 255 + 31) // 63
            scanlines[output_offset + 2] = (blue * 255 + 15) // 31
            output_offset += 3

    signature = b"\x89PNG\r\n\x1a\n"
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        signature
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9))
        + png_chunk(b"IEND", b"")
    )


def write_all(file_descriptor: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = os.write(file_descriptor, data[offset:])
        if written <= 0:
            raise CaptureError("could not write screenshot command to serial device")
        offset += written


def configure_serial(file_descriptor: int) -> None:
    tty.setraw(file_descriptor, when=termios.TCSANOW)
    attributes = termios.tcgetattr(file_descriptor)
    attributes[2] |= termios.CLOCAL | termios.CREAD
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    termios.tcsetattr(file_descriptor, termios.TCSANOW, attributes)


def capture_from_port(port: str, tile: int | None = None) -> tuple[int, int, bytes]:
    try:
        file_descriptor = os.open(port, os.O_RDWR | os.O_NOCTTY)
    except OSError as error:
        raise CaptureError(f"could not open serial port {port}: {error}") from error

    original_attributes = None
    try:
        original_attributes = termios.tcgetattr(file_descriptor)
        configure_serial(file_descriptor)
        termios.tcflush(file_descriptor, termios.TCIFLUSH)
        reader = SerialReader(file_descriptor)
        if tile is not None:
            write_all(file_descriptor, tile_command(tile))
            wait_for_tile_selection(reader, tile)
            time.sleep(TILE_REFRESH_DELAY_SECONDS)
        write_all(file_descriptor, b"screenshot\n")
        return read_screenshot(reader)
    except OSError as error:
        raise CaptureError(f"serial communication failed on {port}: {error}") from error
    finally:
        if original_attributes is not None:
            try:
                termios.tcsetattr(
                    file_descriptor, termios.TCSANOW, original_attributes
                )
            except (OSError, termios.error):
                pass
        os.close(file_descriptor)


def default_output_path() -> Path:
    project_root = Path(__file__).resolve().parent.parent
    return project_root / "artifacts" / "screenshot.png"


def run_self_test() -> None:
    pixels = struct.pack("<4H", 0xF800, 0x07E0, 0x001F, 0xFFFF)
    encoded = encode_rgb565le_png(2, 2, pixels)
    if not encoded.startswith(b"\x89PNG\r\n\x1a\n"):
        raise CaptureError("self-test failed: invalid PNG signature")

    offset = 8
    chunks: dict[bytes, bytes] = {}
    while offset < len(encoded):
        length = struct.unpack_from(">I", encoded, offset)[0]
        chunk_type = encoded[offset + 4 : offset + 8]
        data_start = offset + 8
        data_end = data_start + length
        data = encoded[data_start:data_end]
        declared_crc = struct.unpack_from(">I", encoded, data_end)[0]
        actual_crc = zlib.crc32(chunk_type + data) & 0xFFFFFFFF
        if declared_crc != actual_crc:
            raise CaptureError(f"self-test failed: bad {chunk_type!r} CRC")
        chunks[chunk_type] = data
        offset = data_end + 4

    if struct.unpack(">II", chunks[b"IHDR"][:8]) != (2, 2):
        raise CaptureError("self-test failed: incorrect PNG dimensions")
    expected_scanlines = bytes(
        [0, 255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 255, 255, 255]
    )
    if zlib.decompress(chunks[b"IDAT"]) != expected_scanlines:
        raise CaptureError("self-test failed: RGB565 conversion mismatch")
    if chunks.get(b"IEND") != b"":
        raise CaptureError("self-test failed: missing PNG end chunk")

    if parse_tile_argument("0") != 0 or parse_tile_argument("5") != 5:
        raise CaptureError("self-test failed: valid tile range rejected")
    for invalid_tile in (-1, 6):
        try:
            validate_tile_index(invalid_tile)
        except CaptureError:
            pass
        else:
            raise CaptureError("self-test failed: invalid tile range accepted")
    try:
        parse_tile_argument("weather")
    except argparse.ArgumentTypeError:
        pass
    else:
        raise CaptureError("self-test failed: nonnumeric tile accepted")
    if tile_command(3) != b"tile 3\n":
        raise CaptureError("self-test failed: incorrect tile command")
    if not parse_tile_response(b"tile_selected=3\r\n", 3):
        raise CaptureError("self-test failed: tile acknowledgement rejected")
    if parse_tile_response(b"heartbeat uptime_ms=1\n", 3) is not None:
        raise CaptureError("self-test failed: unrelated serial line accepted")
    for invalid_response in (b"tile_selected=4\n", b"tile_error=invalid_index\n"):
        try:
            parse_tile_response(invalid_response, 3)
        except CaptureError:
            pass
        else:
            raise CaptureError("self-test failed: bad tile response accepted")
    print("self-test passed: PNG encoding 2 x 2 and tile protocol 0..5")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture the Project Jarvis LVGL framebuffer as a PNG."
    )
    parser.add_argument("--port", help="USB serial tty, for example /dev/cu.usbmodem1101")
    parser.add_argument("--output", type=Path, help="destination PNG path")
    parser.add_argument(
        "--tile",
        type=parse_tile_argument,
        metavar="0..5",
        help="select a deterministic UI tile before capture",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="exercise PNG encoding without opening hardware or writing a file",
    )
    arguments = parser.parse_args()
    if not arguments.self_test and not arguments.port:
        parser.error("--port is required unless --self-test is used")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.self_test:
            run_self_test()
            return 0

        width, height, pixels = capture_from_port(arguments.port, arguments.tile)
        png = encode_rgb565le_png(width, height, pixels)
        output = arguments.output or default_output_path()
        output = output.expanduser().resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(png)
        print(f"Captured {width} x {height} PNG: {output}")
        return 0
    except (CaptureError, OSError, KeyError, struct.error, zlib.error) as error:
        print(f"capture failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

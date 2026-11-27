#!/usr/bin/env python3
"""cmake/ConfigOptionsVideoToolbox.cmake 에 tvOS 분기를 추가한다.

상류의 게이트는 CMAKE_SYSTEM_NAME 이 Darwin(macOS) 또는 iOS 일 때만 켠다.
tvOS 는 두 분기 어디에도 걸리지 않아 기본 OFF 가 되고, 강제로 켜면 FATAL_ERROR 가 난다.

이것은 코드 제약이 아니라 CMake 조건이다 — 디코더(h264_videotoolbox.m)가 쓰는
VTDecompressionSession·CMSampleBuffer API 는 tvOS SDK 에 있다.
그래서 조건에 한 분기를 더해 실제로 컴파일되는지만 확인한다.

앵커를 못 찾으면 아무것도 고치지 않고 그대로 보고한다. 상류가 바뀐 것을
조용히 넘기지 않기 위해서다.
"""
import sys

ANCHOR = (
    'elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")\n'
    "  set(_videotoolbox_platform_supported ON)\n"
    '  set(_videotoolbox_minimum_version "8.0")\n'
    "endif()"
)

REPLACEMENT = (
    'elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")\n'
    "  set(_videotoolbox_platform_supported ON)\n"
    '  set(_videotoolbox_minimum_version "8.0")\n'
    'elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "tvOS")\n'
    "  set(_videotoolbox_platform_supported ON)\n"
    '  set(_videotoolbox_minimum_version "10.2")\n'
    "endif()"
)


def main() -> int:
    if len(sys.argv) != 2:
        print("사용법: patch_videotoolbox.py <ConfigOptionsVideoToolbox.cmake>")
        return 2
    path = sys.argv[1]
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as exc:
        print(f"읽기실패:{exc}")
        return 1
    if "CMAKE_SYSTEM_NAME STREQUAL \"tvOS\"" in text:
        print("이미적용")
        return 0
    if ANCHOR not in text:
        print("앵커없음")
        return 1
    open(path, "w", encoding="utf-8").write(text.replace(ANCHOR, REPLACEMENT))
    print("패치됨")
    return 0


if __name__ == "__main__":
    sys.exit(main())

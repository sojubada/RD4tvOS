# RD4tvOS Probe — 1a 시험용 tvOS 앱
#
# 이 앱은 화면을 그리지 않는다. NTLM 토큰이 tvOS 에서 생성되는지만 확인하고
# 결과를 화면과 로그에 남긴다. 접속(1b)은 다음 단계다.
#
# 1a 는 winpr 만 링크한다 — SSPI/NTLM 은 전부 winpr 안에 있다.
# libfreerdp 는 1b 에서 들어온다. 의존을 작게 유지해 실패 지점을 좁힌다.
cmake_minimum_required(VERSION 3.20)
project(rd4tvos_probe OBJC C)

set(WINPR_INCLUDE_DIR "" CACHE PATH "winpr 헤더 디렉터리 (winpr/sspi.h 를 담은 상위)")
set(WINPR_LIBRARY "" CACHE FILEPATH "libwinpr3.a 경로")
set(OPENSSL_ROOT "" CACHE PATH "tvOS OpenSSL prefix (include/, lib/)")
set(APP_VERSION "0.1.0" CACHE STRING "표시 버전")
set(APP_BUILD "1" CACHE STRING "빌드 번호")

foreach(v WINPR_INCLUDE_DIR WINPR_LIBRARY OPENSSL_ROOT)
  if(NOT ${v})
    message(FATAL_ERROR "${v} 를 지정해야 한다. app/build.sh 를 쓰면 자동으로 채워진다.")
  endif()
endforeach()

set(MACOSX_BUNDLE_EXECUTABLE_NAME rd4tvos-probe)
set(MACOSX_BUNDLE_GUI_IDENTIFIER com.sojubada.rd4tvos.probe)
set(MACOSX_BUNDLE_BUNDLE_NAME "RD4tvOS Probe")
set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_VERSION})
set(MACOSX_BUNDLE_BUNDLE_VERSION ${APP_BUILD})
configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)

add_executable(rd4tvos-probe MACOSX_BUNDLE
  src/main.m
  src/AppDelegate.m
  src/DiagViewController.m
  src/DiagLog.m
  src/KeyboardProbe.m
  src/NtlmSelfTest.c
)

target_include_directories(rd4tvos-probe PRIVATE
  src
  ${WINPR_INCLUDE_DIR}
  ${OPENSSL_ROOT}/include
)

find_library(FW_UIKIT UIKit REQUIRED)
find_library(FW_FOUNDATION Foundation REQUIRED)
find_library(FW_GAMECONTROLLER GameController REQUIRED)
find_library(FW_SECURITY Security REQUIRED)
find_library(FW_CF CoreFoundation REQUIRED)

target_link_libraries(rd4tvos-probe PRIVATE
  ${WINPR_LIBRARY}
  ${OPENSSL_ROOT}/lib/libssl.a
  ${OPENSSL_ROOT}/lib/libcrypto.a
  z
  iconv
  ${FW_UIKIT}
  ${FW_FOUNDATION}
  ${FW_GAMECONTROLLER}
  ${FW_SECURITY}
  ${FW_CF}
)

set_target_properties(rd4tvos-probe PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
  XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_ENABLE_BITCODE NO
)

target_compile_options(rd4tvos-probe PRIVATE -fobjc-arc -Wall)

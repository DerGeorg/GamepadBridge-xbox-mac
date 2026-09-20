cmake_minimum_required(VERSION 3.16)
project(gamepadbridge CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Output backend:
#   console   -> Stage 1: print controller input to the terminal (default)
#   iohid     -> Stage 2: virtual HID gamepad via IOHIDUserDevice (userspace,
#                no system extension; needs the hid.virtual.device entitlement
#                or root — see driverkit/README.md)
#   corehid   -> Stage 2: virtual HID gamepad via CoreHID (macOS 15+, Apple's
#                supported API; Swift shim + same hid.virtual.device entitlement)
#   driverkit -> Stage 2: virtual HID gamepad via a DriverKit system extension
set(XOW_BACKEND "console" CACHE STRING
    "Output backend: console, iohid, corehid or driverkit")

# Menu bar UI. Needs Swift and AppKit, so it rides along with the corehid
# backend; any other backend silently does without it.
option(XOW_MENUBAR "Build the menu bar UI (corehid backend only)" ON)

# Marketing version for the bundle. XOW_VERSION is a git describe and is not
# a valid CFBundleShortVersionString.
set(XOW_RELEASE_VERSION "1.0.0" CACHE STRING "Release version for the bundle")

# Default firmware location. Override at runtime with the XOW_FIRMWARE env var.
set(XOW_FIRMWARE_PATH "/usr/local/share/gamepadbridge/xow_dongle.bin"
    CACHE STRING "Default path to the extracted dongle firmware blob")

# Version string from git, if available.
find_package(Git QUIET)
set(XOW_VERSION "dev")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE XOW_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(XOW_GIT_VERSION)
        set(XOW_VERSION "${XOW_GIT_VERSION}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

find_package(Threads REQUIRED)

# A distributed app must not depend on Homebrew: linking the dylib from
# /opt/homebrew means it refuses to launch on any Mac that has not installed
# libusb. Static linking is therefore the default, with a fallback for the
# unusual case of a libusb built without the archive.
option(XOW_STATIC_LIBUSB
    "Link libusb statically so the app runs without Homebrew" ON)

set(LIBUSB_LINK ${LIBUSB_LIBRARIES})

if(XOW_STATIC_LIBUSB)
    find_library(LIBUSB_ARCHIVE
        NAMES libusb-1.0.a
        PATHS ${LIBUSB_LIBRARY_DIRS}
        NO_DEFAULT_PATH)

    if(LIBUSB_ARCHIVE)
        # libusb's darwin backend pulls these in itself; with the dylib they
        # come along automatically, with the archive they must be named.
        find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
        find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        find_library(SECURITY_FRAMEWORK Security REQUIRED)

        set(LIBUSB_LINK
            ${LIBUSB_ARCHIVE}
            objc
            ${IOKIT_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
            ${SECURITY_FRAMEWORK})

        message(STATUS "Linking libusb statically: ${LIBUSB_ARCHIVE}")
    else()
        message(WARNING
            "XOW_STATIC_LIBUSB is on but no libusb-1.0.a was found - falling "
            "back to the shared library. The resulting app will only run on "
            "machines that have libusb installed.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    src/main.cpp
    src/dongle/usb.cpp
    src/dongle/mt76.cpp
    src/dongle/dongle.cpp
    src/controller/gip.cpp
    src/controller/controller.cpp
    src/utils/log.cpp
    src/status.cpp
    src/firmware.cpp
)

set(BACKEND_FRAMEWORKS "")

if(XOW_BACKEND STREQUAL "console")
    list(APPEND CORE_SOURCES src/output_console.cpp)
elseif(XOW_BACKEND STREQUAL "iohid")
    list(APPEND CORE_SOURCES src/output_iohid.cpp)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    list(APPEND BACKEND_FRAMEWORKS
        ${COREFOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK})
    message(STATUS "IOHID backend selected — virtual gamepad via "
                   "IOHIDUserDevice (run as root or with the "
                   "hid.virtual.device entitlement).")
elseif(XOW_BACKEND STREQUAL "corehid")
    # CoreHID is Swift-only, so this backend is a C++ file plus a Swift shim.
    # Swift is enabled here rather than in project() so the other backends keep
    # working with generators that have no Swift support.
    enable_language(Swift)
    list(APPEND CORE_SOURCES src/output_corehid.cpp src/corehid_shim.swift)
    find_library(COREHID_FRAMEWORK CoreHID REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    list(APPEND BACKEND_FRAMEWORKS
        ${COREHID_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK})

    if(XOW_MENUBAR)
        list(APPEND CORE_SOURCES src/menubar.swift src/update_check.swift)
        find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
        list(APPEND BACKEND_FRAMEWORKS ${APPKIT_FRAMEWORK})
        set(XOW_MENUBAR_ACTIVE ON)
    endif()
    message(STATUS "CoreHID backend selected — virtual gamepad via "
                   "HIDVirtualDevice (needs the hid.virtual.device "
                   "entitlement; use -G Xcode so the app gets signed).")
elseif(XOW_BACKEND STREQUAL "driverkit")
    list(APPEND CORE_SOURCES src/output_driverkit.cpp)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    list(APPEND BACKEND_FRAMEWORKS
        ${COREFOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK})
    message(STATUS "DriverKit backend selected — requires the virtual HID "
                   "extension from driverkit/ to be built and activated.")
else()
    message(FATAL_ERROR "Unknown XOW_BACKEND '${XOW_BACKEND}' "
                        "(expected 'console', 'iohid', 'corehid' or "
                        "'driverkit')")
endif()

add_executable(gamepadbridge ${CORE_SOURCES})

target_include_directories(gamepadbridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUSB_INCLUDE_DIRS}
)

target_link_directories(gamepadbridge PRIVATE ${LIBUSB_LIBRARY_DIRS})

target_link_libraries(gamepadbridge PRIVATE
    ${LIBUSB_LINK}
    Threads::Threads
    ${BACKEND_FRAMEWORKS}
)

# NOTE: these must stay namespaced. A bare VERSION/FIRMWARE macro leaks into
# every header the compiler sees — including SDK headers that use those words as
# struct members (IOKit's SCSICmds_INQUIRY_Definitions.h has "UInt8 VERSION;"),
# which breaks Swift's IOKit module build for the corehid backend.
target_compile_definitions(gamepadbridge PRIVATE
    GAMEPADBRIDGE_VERSION="${XOW_VERSION}"
    GAMEPADBRIDGE_FIRMWARE="${XOW_FIRMWARE_PATH}"
)

target_compile_options(gamepadbridge PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>)

if(XOW_BACKEND STREQUAL "corehid")
    set_target_properties(gamepadbridge PROPERTIES
        XCODE_ATTRIBUTE_SWIFT_VERSION "5.0")
endif()

if(XOW_MENUBAR_ACTIVE)
    target_compile_definitions(gamepadbridge PRIVATE GAMEPADBRIDGE_MENUBAR)

    # menubar.swift calls into the C half of the status board.
    set_target_properties(gamepadbridge PROPERTIES
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
            "${CMAKE_SOURCE_DIR}/src/gamepadbridge-bridging.h")
endif()

# ---------------------------------------------------------------------------
# Optional: signed .app bundle for the IOHID/DriverKit backends.
#
# The hid.virtual.device entitlement is restricted and must be authorised by
# an Apple-issued provisioning profile, which only attaches to a bundle. Build
# this with the Xcode generator + automatic signing:
#
#   cmake -G Xcode -DXOW_BACKEND=iohid -DXOW_MACOS_APP=ON \
#         -DXOW_TEAM_ID=XXXXXXXXXX -S . -B build-xcode
#   xcodebuild -project build-xcode/gamepadbridge.xcodeproj \
#              -configuration Release -allowProvisioningUpdates
# ---------------------------------------------------------------------------
option(XOW_MACOS_APP "Build a signed .app bundle (use with -G Xcode)" OFF)
set(XOW_TEAM_ID "" CACHE STRING "Apple Developer Team ID for signing")
set(XOW_BUNDLE_ID "at.dergeorg.gamepadbridge" CACHE STRING "App bundle identifier")

# Development builds sign themselves automatically. A release has to be
# explicit: Developer ID, a specific profile, and the hardened runtime, which
# notarization refuses to proceed without.
set(XOW_SIGN_STYLE "Automatic" CACHE STRING "Code sign style: Automatic or Manual")
set(XOW_CODESIGN_IDENTITY "" CACHE STRING "Signing identity, e.g. 'Developer ID Application'")
set(XOW_PROVISIONING_PROFILE "" CACHE STRING "Provisioning profile name or UUID")

if(XOW_MACOS_APP)
    set(GPB_ICON ${CMAKE_SOURCE_DIR}/packaging/GamepadBridge.icns)
    target_sources(gamepadbridge PRIVATE ${GPB_ICON})
    set_source_files_properties(${GPB_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

    set_target_properties(gamepadbridge PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER ${XOW_BUNDLE_ID}
        MACOSX_BUNDLE_BUNDLE_NAME "GamepadBridge"
        MACOSX_BUNDLE_ICON_FILE "GamepadBridge.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${XOW_RELEASE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${XOW_RELEASE_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT
            "GPL-2.0-or-later. Based on xow by Medusalix."
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_SOURCE_DIR}/packaging/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${XOW_BUNDLE_ID}
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "${XOW_SIGN_STYLE}"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${XOW_TEAM_ID}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"

        # Xcode signs like a development build unless told otherwise, and
        # notarization rejects both results: it injects a get-task-allow
        # entitlement (which would let anything attach a debugger to the
        # shipped app) and it signs with --timestamp=none.
        XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "NO"
        XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--timestamp"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${CMAKE_SOURCE_DIR}/packaging/gamepadbridge.entitlements"

        # The distributed bundle carries the project's proper name; the
        # command-line build keeps the lowercase binary the docs refer to.
        OUTPUT_NAME "GamepadBridge"
    )

    if(XOW_CODESIGN_IDENTITY)
        set_target_properties(gamepadbridge PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${XOW_CODESIGN_IDENTITY}")
    endif()

    if(XOW_PROVISIONING_PROFILE)
        set_target_properties(gamepadbridge PROPERTIES
            XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER
                "${XOW_PROVISIONING_PROFILE}")
    endif()
endif()

message(STATUS "gamepadbridge ${XOW_VERSION}  backend=${XOW_BACKEND}  firmware=${XOW_FIRMWARE_PATH}")

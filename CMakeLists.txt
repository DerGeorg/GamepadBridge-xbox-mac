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
#   driverkit -> Stage 2: virtual HID gamepad via a DriverKit system extension
set(XOW_BACKEND "console" CACHE STRING "Output backend: console, iohid or driverkit")

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
                        "(expected 'console', 'iohid' or 'driverkit')")
endif()

add_executable(gamepadbridge ${CORE_SOURCES})

target_include_directories(gamepadbridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUSB_INCLUDE_DIRS}
)

target_link_directories(gamepadbridge PRIVATE ${LIBUSB_LIBRARY_DIRS})

target_link_libraries(gamepadbridge PRIVATE
    ${LIBUSB_LIBRARIES}
    Threads::Threads
    ${BACKEND_FRAMEWORKS}
)

target_compile_definitions(gamepadbridge PRIVATE
    VERSION="${XOW_VERSION}"
    FIRMWARE="${XOW_FIRMWARE_PATH}"
)

target_compile_options(gamepadbridge PRIVATE -Wall -Wextra)

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

if(XOW_MACOS_APP)
    set_target_properties(gamepadbridge PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER ${XOW_BUNDLE_ID}
        MACOSX_BUNDLE_BUNDLE_NAME "GamepadBridge"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${XOW_BUNDLE_ID}
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${XOW_TEAM_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${CMAKE_SOURCE_DIR}/packaging/gamepadbridge.entitlements"
    )
endif()

message(STATUS "gamepadbridge ${XOW_VERSION}  backend=${XOW_BACKEND}  firmware=${XOW_FIRMWARE_PATH}")

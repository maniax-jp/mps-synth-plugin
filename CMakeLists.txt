cmake_minimum_required(VERSION 3.25)

# macOS は Apple Silicon(arm64)と Intel(x86_64)のユニバーサル・バイナリを既定にする
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "macOS deployment target" FORCE)
    endif()
endif()

project(mps_plugins VERSION 0.1.0 LANGUAGES C CXX)
# VSTGUI と SDK の macOS 部分は Objective-C++ を使う。サブディレクトリだけで有効にすると Ninja / Makefile の生成で
# CMAKE_OBJCXX_COMPILE_OBJECT が見つからないので、最上位で有効にする
if(APPLE)
    enable_language(OBJC OBJCXX)
    # GitHub の macOS ランナーには Mono.framework があり、Expat のヘッダーがそちらから見つかってしまう。
    # macOS SDK のヘッダーとライブラリをそろえて使うため、フレームワークは後回しに探す
    set(CMAKE_FIND_FRAMEWORK LAST)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MPS_BUILD_VST3 "VST3 プラグインをビルドする" ON)
option(MPS_BUILD_GUI "状態表示の GUI(VSTGUI)を付ける" ON)
set(VST3_SDK_ROOT "" CACHE PATH "VST3 SDK の場所(空なら GitHub から取得)")

include(FetchContent)

# ---------------------------------------------------------------- JSON(nlohmann/json, MIT)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------- DSP コア(VST3 SDK に依存しない)
add_library(mps_core STATIC
    core/params.cpp core/synth.cpp core/fx.cpp core/presets.cpp core/control.cpp core/control_server.cpp)
target_include_directories(mps_core PUBLIC core)
target_link_libraries(mps_core PUBLIC nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(mps_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(mps_core PUBLIC ws2_32)
    target_compile_definitions(mps_core PUBLIC NOMINMAX _USE_MATH_DEFINES)
endif()
if(MSVC)
    target_compile_options(mps_core PUBLIC /utf-8)
endif()

add_executable(mps_render tools/mps_render.cpp)
target_link_libraries(mps_render PRIVATE mps_core)

# ---------------------------------------------------------------- VST3
if(MPS_BUILD_VST3)
    add_subdirectory(vst3)
endif()

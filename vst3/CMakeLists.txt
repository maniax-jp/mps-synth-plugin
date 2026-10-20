# ---------------------------------------------------------------- VST3 SDK(MIT ライセンス、3.8 以降)
if(VST3_SDK_ROOT)
    set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
else()
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.0_build_66
        GIT_SHALLOW TRUE
        GIT_SUBMODULES base cmake pluginterfaces public.sdk vstgui4
        SOURCE_SUBDIR not-a-directory)  # 取得だけして、add_subdirectory は下で自分で行う
    FetchContent_MakeAvailable(vst3sdk)
endif()

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ${MPS_BUILD_GUI} CACHE BOOL "" FORCE)
# ビルドしたらユーザーの VST3 フォルダへリンクを張る動作は既定で切る(CI で失敗しないように)。
# 手元で張りたいときは -DSMTG_CREATE_PLUGIN_LINK=ON
if(NOT DEFINED SMTG_CREATE_PLUGIN_LINK)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk EXCLUDE_FROM_ALL)
smtg_enable_vst3_sdk()

set(mps_vst3_sources
    entry.cpp processor.cpp processor.h controller.cpp controller.h cids.h)
if(MPS_BUILD_GUI)
    list(APPEND mps_vst3_sources status_editor.cpp status_editor.h)
endif()

smtg_add_vst3plugin(MPS ${mps_vst3_sources})
target_link_libraries(MPS PRIVATE sdk mps_core)
target_include_directories(MPS PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MPS_BUILD_GUI)
    target_link_libraries(MPS PRIVATE vstgui_support)
    target_compile_definitions(MPS PRIVATE MPS_WITH_GUI=1)
endif()
if(SMTG_MAC)
    smtg_target_set_bundle(MPS
        BUNDLE_IDENTIFIER "me.maniax.mps"
        COMPANY_NAME "Maniax")
endif()

# SMTG_RUN_VST_VALIDATOR(既定 ON)のとき、SDK がビルド後に validator で MPS.vst3 を検査する

# DAW の代わりに MPS.vst3 を読み込んで実時間で回すテスト用ホスト
option(MPS_BUILD_TESTHOST "テスト用ホスト mps_testhost をビルドする" ON)
if(MPS_BUILD_TESTHOST)
    set(th_sources ../tools/testhost.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp)
    if(SMTG_MAC)
        list(APPEND th_sources ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
        set_source_files_properties(${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    elseif(SMTG_LINUX)
        list(APPEND th_sources ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
    elseif(SMTG_WIN)
        list(APPEND th_sources ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
    endif()
    add_executable(mps_testhost ${th_sources})
    target_link_libraries(mps_testhost PRIVATE sdk_hosting mps_core)
    if(APPLE)
        find_library(COCOA_FRAMEWORK Cocoa)
        target_link_libraries(mps_testhost PRIVATE ${COCOA_FRAMEWORK})
    endif()
    add_dependencies(mps_testhost MPS)
endif()

# ステータス GUI をオフスクリーンで PNG にする開発用ツール(Linux)
if(MPS_BUILD_GUI AND SMTG_LINUX)
    add_executable(mps_gui_snapshot EXCLUDE_FROM_ALL ../tools/gui_snapshot.cpp controller.cpp status_editor.cpp
        entry.cpp processor.cpp ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp)
    target_link_libraries(mps_gui_snapshot PRIVATE sdk vstgui_support mps_core ${CMAKE_DL_LIBS})
    target_include_directories(mps_gui_snapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(mps_gui_snapshot PRIVATE MPS_WITH_GUI=1)
endif()

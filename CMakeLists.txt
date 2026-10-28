cmake_minimum_required(VERSION 3.24)
project(qatention VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Widgets Test)
qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

add_library(aspeed STATIC third_party/aspeed/decoder.c)
set_target_properties(aspeed PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The upstream C source uses GNU variadic macros; Clang/GCC are the supported compilers.
add_library(qatention_core STATIC
    src/core/decoder.cpp src/core/decoder.h
    src/core/protocol.cpp src/core/protocol.h
    src/core/session.cpp src/core/session.h
    src/core/profiles.cpp src/core/profiles.h
    src/core/debuglog.cpp src/core/debuglog.h
    src/core/media.cpp src/core/media.h)
target_include_directories(qatention_core PUBLIC src)
target_link_libraries(qatention_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network PRIVATE aspeed)
if(WIN32)
    target_link_libraries(qatention_core PRIVATE crypt32)
endif()
qt_add_resources(qatention_core vendor_certificate PREFIX / FILES resources/client.pfx)

qt_add_executable(qatention-capture src/capture.cpp)
target_link_libraries(qatention-capture PRIVATE qatention_core)
install(TARGETS qatention-capture RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

qt_add_executable(qatention src/main.cpp src/ui/console.cpp src/ui/console.h src/ui/mainwindow.cpp src/ui/mainwindow.h)
target_link_libraries(qatention PRIVATE qatention_core Qt6::Widgets)
set_target_properties(qatention PROPERTIES
    WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.libc6.qatention
    MACOSX_BUNDLE_BUNDLE_NAME QATENtion
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
install(TARGETS qatention BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Retain the headless platform so installed packages can run the same UI/TLS smoke test.
if(APPLE)
    set(headless_plugin_dir qatention.app/Contents/PlugIns/platforms)
elseif(WIN32)
    set(headless_plugin_dir ${CMAKE_INSTALL_BINDIR}/platforms)
else()
    set(headless_plugin_dir plugins/platforms)
endif()
install(FILES $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin> DESTINATION ${headless_plugin_dir})
if(NOT CMAKE_CROSSCOMPILING)
    qt_generate_deploy_app_script(TARGET qatention OUTPUT_SCRIPT deploy_script)
    install(SCRIPT ${deploy_script})
endif()
install(FILES LICENSE README.md THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/qatention)
install(DIRECTORY third_party/aspeed DESTINATION ${CMAKE_INSTALL_DATADIR}/qatention/source)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(ui_tests tests/ui_tests.cpp src/ui/mainwindow.cpp src/ui/mainwindow.h src/ui/console.cpp src/ui/console.h)
    target_link_libraries(ui_tests PRIVATE qatention_core Qt6::Widgets Qt6::Test)
    add_test(NAME ui COMMAND ui_tests)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    qt_add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE qatention_core Qt6::Test)
    add_test(NAME core COMMAND core_tests)
    set_tests_properties(core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_test(NAME decoder_cli COMMAND qatention-capture --check-native-decoder)
    add_test(NAME app_smoke COMMAND qatention --smoke-test)
    set_tests_properties(app_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 15)
endif()

cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QATENTION_VERSION LIMIT_COUNT 1)
if(NOT QATENTION_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)(-[A-Za-z0-9][A-Za-z0-9.-]*)?$")
    message(FATAL_ERROR "Invalid VERSION")
endif()
set(QATENTION_NUMERIC_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
project(qatention VERSION ${QATENTION_NUMERIC_VERSION} LANGUAGES C CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(QATENTION_MOBILE "Build the Qt Quick mobile interface (also available on desktop for testing)" OFF)
if(ANDROID OR QATENTION_MOBILE)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2)
else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Widgets Test)
endif()
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
    src/core/certificatechain.cpp src/core/certificatechain.h
    src/core/debuglog.cpp src/core/debuglog.h
    src/core/media.cpp src/core/media.h
    src/core/videotrace.cpp src/core/videotrace.h)
target_include_directories(qatention_core PUBLIC src)
target_compile_definitions(qatention_core PUBLIC QATENTION_VERSION="${QATENTION_VERSION}")
target_link_libraries(qatention_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network PRIVATE aspeed)
if(WIN32)
    target_link_libraries(qatention_core PRIVATE crypt32)
endif()
qt_add_resources(qatention_core vendor_certificate PREFIX / FILES
    resources/client.pfx resources/client-cert.pem resources/client-key.pem)

if(ANDROID OR QATENTION_MOBILE)
    qt_add_executable(qatention src/mobile/main.cpp src/mobile/mobile.cpp src/mobile/mobile.h)
    target_link_libraries(qatention PRIVATE qatention_core Qt6::Quick Qt6::QuickControls2)
    qt_add_resources(qatention mobile_licenses PREFIX /licenses FILES LICENSE THIRD_PARTY.md)
    qt_add_qml_module(qatention URI QatentionMobile VERSION 1.0 QML_FILES src/mobile/Main.qml src/mobile/AnsiKeys.js)
    if(ANDROID)
        if(NOT EXISTS "${ANDROID_OPENSSL_DIR}/libssl_3.so")
            message(FATAL_ERROR "Set ANDROID_OPENSSL_DIR to the ARM64 OpenSSL library directory")
        endif()
        file(GLOB android_license_files "${ANDROID_OPENSSL_DIR}/*.txt")
        qt_add_resources(qatention android_licenses PREFIX /licenses BASE "${ANDROID_OPENSSL_DIR}" FILES ${android_license_files})
        target_link_options(qatention PRIVATE "-Wl,-z,max-page-size=16384")
        set(QATENTION_ANDROID_VERSION_CODE 2 CACHE STRING "Android update version")
        set_target_properties(qatention PROPERTIES
            QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
            QT_ANDROID_PACKAGE_NAME org.libc6.qatention
            QT_ANDROID_APP_NAME QATENtion
            QT_ANDROID_MIN_SDK_VERSION 28
            QT_ANDROID_TARGET_SDK_VERSION 35
            QT_ANDROID_VERSION_CODE ${QATENTION_ANDROID_VERSION_CODE}
            QT_ANDROID_VERSION_NAME "${QATENTION_VERSION}"
            QT_ANDROID_EXTRA_LIBS "${ANDROID_OPENSSL_DIR}/libcrypto_3.so;${ANDROID_OPENSSL_DIR}/libssl_3.so")
    endif()
    if(NOT ANDROID)
        find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
        enable_testing()
        qt_add_executable(mobile_tests tests/mobile_tests.cpp src/mobile/mobile.cpp src/mobile/mobile.h)
        target_link_libraries(mobile_tests PRIVATE qatention_core Qt6::Quick Qt6::QuickControls2 Qt6::Test)
        qt_add_qml_module(mobile_tests URI MobileTest QML_FILES src/mobile/Main.qml src/mobile/AnsiKeys.js)
        add_test(NAME mobile_view COMMAND mobile_tests)
        set_tests_properties(mobile_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1.5")
    endif()
    return()
endif()

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
if(APPLE)
    set_target_properties(qatention qatention-capture PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
endif()
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
    qt_add_executable(certificatechain_tests tests/certificatechain_tests.cpp src/core/certificatechain.cpp)
    target_include_directories(certificatechain_tests PRIVATE src)
    target_link_libraries(certificatechain_tests PRIVATE Qt6::Network Qt6::Test)
    qt_add_resources(certificatechain_tests test_certificates PREFIX /tls FILES
        tests/certificates/root.pem tests/certificates/issuer.pem tests/certificates/leaf.pem tests/certificates/leaf.key)
    add_test(NAME certificatechain COMMAND certificatechain_tests)
    set_tests_properties(certificatechain PROPERTIES TIMEOUT 30 RESOURCE_LOCK aia_test_port)
    qt_add_executable(ui_tests tests/ui_tests.cpp src/ui/mainwindow.cpp src/ui/mainwindow.h src/ui/console.cpp src/ui/console.h)
    target_link_libraries(ui_tests PRIVATE qatention_core Qt6::Widgets Qt6::Test)
    add_test(NAME ui COMMAND ui_tests)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    qt_add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE qatention_core Qt6::Test)
    qt_add_resources(core_tests core_test_certificates PREFIX /tls FILES
        tests/certificates/root.pem tests/certificates/issuer.pem tests/certificates/leaf.pem tests/certificates/leaf.key)
    add_test(NAME core COMMAND core_tests)
    set_tests_properties(core PROPERTIES RESOURCE_LOCK aia_test_port ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_test(NAME decoder_cli COMMAND qatention-capture --check-native-decoder)
    add_test(NAME app_smoke COMMAND qatention --smoke-test)
    set_tests_properties(app_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 15)
endif()

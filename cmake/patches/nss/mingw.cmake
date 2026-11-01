# NSS's Make build already supports MinGW, including PE DLLs and import
# libraries. Its GYP Windows configuration assumes MSVC. Use Make only for
# Linux-hosted x64 MinGW; keep the upstream recipe for every other target.
if(NOT CMAKE_HOST_LINUX OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    message(FATAL_ERROR "The qatention NSS MinGW overlay requires a Linux host and x64 target")
endif()
# freebl and nssckbi are runtime-loaded modules and have no import libraries.
set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
# Match the MSVCRT ABI of Ubuntu's MinGW cross-compiler and its NSPR build.
set(VCPKG_POLICY_ALLOW_OBSOLETE_MSVCRT enabled)
find_program(nss_make NAMES gmake make REQUIRED)
find_program(nss_cc x86_64-w64-mingw32-gcc-posix REQUIRED)
find_program(nss_cxx x86_64-w64-mingw32-g++-posix REQUIRED)
find_program(nss_ar x86_64-w64-mingw32-ar REQUIRED)
find_program(nss_ranlib x86_64-w64-mingw32-ranlib REQUIRED)
find_program(nss_windres x86_64-w64-mingw32-windres REQUIRED)
set(nss_install "${CURRENT_HOST_INSTALLED_DIR}/manual-tools/nspr/nsinstall")
if(NOT EXISTS "${nss_install}")
    message(FATAL_ERROR "Missing host NSPR nsinstall: ${nss_install}")
endif()

set(nss_configurations Release)
if(NOT VCPKG_BUILD_TYPE)
    list(APPEND nss_configurations Debug)
endif()
foreach(configuration IN LISTS nss_configurations)
    set(dependency_lib "${CURRENT_INSTALLED_DIR}/lib")
    set(package_prefix "${CURRENT_PACKAGES_DIR}")
    set(optimization BUILD_OPT=1)
    set(object_directory WINNT_x86_64_64_OPT.OBJ)
    if(configuration STREQUAL "Debug")
        set(dependency_lib "${CURRENT_INSTALLED_DIR}/debug/lib")
        set(package_prefix "${CURRENT_PACKAGES_DIR}/debug")
        set(optimization BUILD_OPT=)
        set(object_directory WINNT_x86_64_64_DBG.OBJ)
    endif()
    unset(nss_zlib CACHE)
    find_library(nss_zlib NAMES zs zsd z zlib zlibd PATHS "${dependency_lib}" NO_DEFAULT_PATH REQUIRED)
    file(READ "${SOURCE_PATH}/nss/pkg/pkg-config/nss.pc.in" pkgconfig)
    string(REPLACE "%exec_prefix%" "\${prefix}" pkgconfig "${pkgconfig}")
    string(REPLACE "%libdir%" "\${prefix}/lib" pkgconfig "${pkgconfig}")
    string(REPLACE "%includedir%" "\${prefix}/include/nss" pkgconfig "${pkgconfig}")
    string(REPLACE "%NSS_VERSION%" "${VERSION}" pkgconfig "${pkgconfig}")
    string(REPLACE "%NSPR_VERSION%" "4.36" pkgconfig "${pkgconfig}")
    file(WRITE "${package_prefix}/lib/pkgconfig/nss.pc" "${pkgconfig}")
    # Separate source/build trees prevent configuration outputs from mixing.
    set(build_root "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-make-${configuration}")
    file(REMOVE_RECURSE "${build_root}")
    file(MAKE_DIRECTORY "${build_root}")
    file(COPY "${SOURCE_PATH}/nss" DESTINATION "${build_root}")
    # NSS captures recursive Make output as object filenames. Ubuntu's Make
    # adds directory banners there unless disabled at the top-level invocation.
    vcpkg_execute_required_process(
        COMMAND "${nss_make}" --no-print-directory "-j${VCPKG_CONCURRENCY}" all
            OS_TARGET=WINNT OS_ARCH=WINNT OS_RELEASE= CPU_ARCH=x86_64
            USE_64=1 NS_USE_GCC=1 CROSS_COMPILE=1 "${optimization}"
            NSS_DISABLE_GTESTS=1 NSS_DISABLE_DBM=1 NSS_DISABLE_LIBPKIX=
            NSS_USE_SYSTEM_SQLITE=1 USE_SYSTEM_ZLIB=1
            # Build the shipped command-line tools, not NSS's separate test
            # executables (the upstream GYP recipe also disables tests).
            "NSS_CMD_TARGETS=shlibsign certutil crlutil modutil pk12util pwdecrypt signtool signver smimetools ssltap symkeyutil validation vfychain"
            "ZLIB_LIBS=${nss_zlib}"
            "NSPR_INCLUDE_DIR=${CURRENT_INSTALLED_DIR}/include/nspr"
            "NSPR_LIB_DIR=${dependency_lib}"
            "SQLITE_INCLUDE_DIR=${CURRENT_INSTALLED_DIR}/include"
            "SQLITE_LIB_DIR=${dependency_lib}"
            "CC=${nss_cc}" "CCC=${nss_cxx}" "AR=${nss_ar} cr $@"
            "RANLIB=${nss_ranlib}" "RC=${nss_windres} -O coff --use-temp-file"
            "NSINSTALL=${nss_install}"
            # Match the upstream port's sign_libs=0 cross-build policy.
            CHECKLIBS= CHECKLOC=
        WORKING_DIRECTORY "${build_root}/nss"
        LOGNAME "make-${TARGET_TRIPLET}-${configuration}")
    set(distribution "${build_root}/dist/${object_directory}")
    file(GLOB runtime_libraries "${distribution}/lib/*.dll")
    list(FILTER runtime_libraries EXCLUDE REGEX "-testlib[.]dll$")
    if(NOT runtime_libraries)
        message(FATAL_ERROR "NSS did not produce MinGW DLLs")
    endif()
    file(INSTALL ${runtime_libraries} DESTINATION "${package_prefix}/bin")
    # Install only import libraries for the shared libraries, not internal
    # static archives which have different dependency/linkage requirements.
    foreach(name nss3 nssutil3 smime3 ssl3 softokn3)
        file(INSTALL "${distribution}/lib/lib${name}.a" DESTINATION "${package_prefix}/lib")
    endforeach()
    if(configuration STREQUAL "Release")
        file(INSTALL "${build_root}/dist/public/nss" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
        file(INSTALL "${build_root}/dist/private/nss/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/nss/private")
        file(GLOB tools "${distribution}/bin/*.exe")
        file(INSTALL ${tools} DESTINATION "${CURRENT_PACKAGES_DIR}/tools/nss")
        # NSS loads softokn/freebl dynamically; import scanning cannot find them.
        file(INSTALL ${runtime_libraries} DESTINATION "${CURRENT_PACKAGES_DIR}/tools/nss")
    endif()
endforeach()
vcpkg_fixup_pkgconfig()
vcpkg_copy_tool_dependencies("${CURRENT_PACKAGES_DIR}/tools/nss")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/nss/COPYING")

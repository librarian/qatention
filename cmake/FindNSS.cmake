# NSS publishes pkg-config metadata, but no CMake package. Locate its public
# headers and shared libraries directly so cross-builds do not query host NSS.
find_path(NSS_INCLUDE_DIR cert.h PATH_SUFFIXES nss)
find_path(NSPR_INCLUDE_DIR prtypes.h PATH_SUFFIXES nspr)
include(SelectLibraryConfigurations)
foreach(name NSS NSPR)
    if(name STREQUAL "NSS")
        set(library_name nss3)
    else()
        set(library_name nspr4)
    endif()
    find_library(${name}_LIBRARY_RELEASE NAMES ${library_name}
        HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    find_library(${name}_LIBRARY_DEBUG NAMES ${library_name}
        HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")
    select_library_configurations(${name})
endforeach()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NSS REQUIRED_VARS
    NSS_INCLUDE_DIR NSPR_INCLUDE_DIR NSS_LIBRARY NSPR_LIBRARY)
if(NSS_FOUND AND NOT TARGET NSS::NSS)
    add_library(NSS::NSS INTERFACE IMPORTED)
    set_target_properties(NSS::NSS PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NSS_INCLUDE_DIR};${NSPR_INCLUDE_DIR}")
    target_link_libraries(NSS::NSS INTERFACE ${NSS_LIBRARY} ${NSPR_LIBRARY})
    if(MINGW AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        # NSPR's Windows prcpucfg.h recognizes the Win32 SDK architecture macro,
        # not GCC's __x86_64__. Do not rely on a prior windows.h include.
        target_compile_definitions(NSS::NSS INTERFACE _AMD64_)
    endif()
endif()

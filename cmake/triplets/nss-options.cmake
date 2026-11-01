# Keep the registry's NSS port; only override its supported build option.
# Do not send this option to NSPR or other configure-based dependencies.
if(PORT STREQUAL "nss")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
    list(APPEND VCPKG_MAKE_CONFIGURE_OPTIONS --enable-libpkix)
endif()
if(PORT STREQUAL "nspr")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

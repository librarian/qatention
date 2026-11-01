# NSS loads softokn/freebl modules at runtime; a linker-only dependency scan
# misses them. Deploy the shared libraries from the selected vcpkg triplet,
# including those modules and NSPR, alongside the application.
if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
    return()
endif()
set(dependency_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
if(ANDROID)
    file(GLOB dependency_libraries "${dependency_prefix}/lib/*.so")
    set_property(TARGET qatention APPEND PROPERTY QT_ANDROID_EXTRA_LIBS ${dependency_libraries})
elseif(WIN32)
    if(MINGW)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/nss/"
            DESTINATION "share/qatention/source/nss-patches")
    endif()
    install(DIRECTORY "${dependency_prefix}/bin/" DESTINATION bin
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel FILES_MATCHING PATTERN "*.dll")
    install(DIRECTORY "${dependency_prefix}/debug/bin/" DESTINATION bin
        CONFIGURATIONS Debug FILES_MATCHING PATTERN "*.dll")
else()
    if(APPLE)
        set(dependency_destination qatention.app/Contents/Frameworks)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/nss/"
            DESTINATION "qatention.app/Contents/Resources/source/nss-patches")
    else()
        set(dependency_destination lib)
    endif()
    install(DIRECTORY "${dependency_prefix}/lib/" DESTINATION "${dependency_destination}"
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        FILES_MATCHING PATTERN "*.so*" PATTERN "*.dylib")
    install(DIRECTORY "${dependency_prefix}/debug/lib/" DESTINATION "${dependency_destination}"
        CONFIGURATIONS Debug
        FILES_MATCHING PATTERN "*.so*" PATTERN "*.dylib")
endif()
foreach(dependency nss nspr spdlog fmt sqlite3 zlib)
    if(ANDROID)
        qt_add_resources(qatention "license_${dependency}" PREFIX "/licenses/${dependency}"
            BASE "${dependency_prefix}/share/${dependency}"
            FILES "${dependency_prefix}/share/${dependency}/copyright")
    else()
        install(FILES "${dependency_prefix}/share/${dependency}/copyright"
            DESTINATION "licenses/${dependency}")
    endif()
endforeach()

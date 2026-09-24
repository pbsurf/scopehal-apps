# libiio 0.26: the last release with the 0.x API, which is the only one scopehal supports.
# (libiio 1.0 changed the API and ABI; upstream master is 1.x.)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO analogdevicesinc/libiio
    REF v0.26
    SHA512 f2febe8223149602e9d34957fb04892ff1d7449abf2923d0428d0db43148445a0b5595eb6d00013687c73001685b6aaaa5aa098ff67f51ec1950200330481bba
    HEAD_REF libiio-v0
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        usb WITH_USB_BACKEND
)

# libiio finds libusb via pkg-config first (with a find_library fallback)
vcpkg_find_acquire_program(PKGCONFIG)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        "-DPKG_CONFIG_EXECUTABLE=${PKGCONFIG}"
        -DWITH_NETWORK_BACKEND=ON
        -DWITH_XML_BACKEND=ON
        # On Windows this uses the bundled mdns code, no extra dependency
        -DHAVE_DNS_SD=ON
        -DWITH_SERIAL_BACKEND=OFF
        -DWITH_ZSTD=OFF
        # Linux-only server side and system integration, not needed by a client
        -DWITH_IIOD=OFF
        -DINSTALL_UDEV_RULE=OFF
        # iio_info etc. are not needed to build ngscopeclient
        -DWITH_TESTS=OFF
        -DWITH_EXAMPLES=OFF
        -DWITH_DOC=OFF
        -DWITH_MAN=OFF
        -DCPP_BINDINGS=OFF
        -DCSHARP_BINDINGS=OFF
        -DPYTHON_BINDINGS=OFF
        -DOSX_FRAMEWORK=OFF
        -DOSX_PACKAGE=OFF
        -DENABLE_PACKAGING=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=ON
    MAYBE_UNUSED_VARIABLES
        INSTALL_UDEV_RULE
        OSX_PACKAGE
        WITH_IIOD
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

# MSVC builds name the import library libiio.lib, but upstream's .pc file says -liio
if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    foreach(pc IN ITEMS
            "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/libiio.pc"
            "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/libiio.pc")
        if(EXISTS "${pc}")
            vcpkg_replace_string("${pc}" "-liio" "-llibiio")
        endif()
    endforeach()
endif()

# Upstream only defines LIBIIO_STATIC while building libiio itself; consumers of a
# static build would otherwise see __declspec(dllimport) declarations
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/iio.h"
        "#   ifdef LIBIIO_STATIC"
        "#   if 1 /* vcpkg: static build */")
endif()

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.txt")

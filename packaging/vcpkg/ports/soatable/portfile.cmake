# Header-only port. Use this directory as a vcpkg overlay:
#   vcpkg install soatable --overlay-ports=packaging/vcpkg/ports
#
# On each tagged release, replace SHA512 below with the value reported by:
#   vcpkg hash <downloaded-tarball>
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO bbalouki/soatable
    REF "v${VERSION}"
    SHA512 0
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSOATABLE_BUILD_TESTS=OFF
        -DSOATABLE_BUILD_EXAMPLES=OFF
        -DSOATABLE_BUILD_BENCHMARKS=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME soatable
    CONFIG_PATH "${CMAKE_INSTALL_LIBDIR}/cmake/soatable"
)

# An interface library installs no binaries.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug" "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

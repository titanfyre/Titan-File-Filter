vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/titanfyre/Titan-File-Filter/releases/download/sdk-v0.1.0/titan-file-filter-sdk-0.1.0.tar.gz"
    FILENAME "titan-file-filter-sdk-0.1.0.tar.gz"
    SHA512 392ca0c707b9825f9599085c590de8bfd96802ffefe4dfd6defe31d72733fa99960fce269f36e66b5a5e917dd944c5a48c5ca253cc9fbcd96c241291aa4a1075
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME TitanFileFilterSDK CONFIG_PATH lib/cmake/TitanFileFilterSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

cmake_minimum_required(VERSION 3.24)
project(TitanFileFilterSDK VERSION 0.1.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanTitanFileFilter STATIC
  "sdk/SDK/FileFilter.cpp"
)
add_library(TitanFileFilterSDK::sdk ALIAS TitanTitanFileFilter)
set_target_properties(TitanTitanFileFilter PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanTitanFileFilter PUBLIC cxx_std_17)
target_compile_definitions(TitanTitanFileFilter PUBLIC )
target_compile_definitions(TitanTitanFileFilter PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanTitanFileFilter PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanTitanFileFilter PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/SDK>"
  "$<INSTALL_INTERFACE:include/SDK>"
)
target_link_libraries(TitanTitanFileFilter PUBLIC advapi32 comctl32 comdlg32 crypt32 fltlib gdi32 newdev ole32 setupapi shell32 user32 wintrust)
install(TARGETS TitanTitanFileFilter EXPORT TitanFileFilterSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/titan-file-filter)
install(EXPORT TitanFileFilterSDKTargets NAMESPACE TitanFileFilterSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TitanFileFilterSDK)
configure_file("TitanFileFilterSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/TitanFileFilterSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/TitanFileFilterSDKConfigVersion.cmake" VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/TitanFileFilterSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/TitanFileFilterSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TitanFileFilterSDK)

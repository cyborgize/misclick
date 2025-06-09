cmake_minimum_required(VERSION 3.16)

project(misclick
    VERSION 1.0.0
    DESCRIPTION "A C library for microcontroller button input debouncing and click detection"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Library target
add_library(misclick
    src/misclick.c
    include/misclick/misclick.h
)

# Set include directories
target_include_directories(misclick PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Private include for source files
target_include_directories(misclick PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation (only when building as main project)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(GNUInstallDirs)

    install(TARGETS misclick
        EXPORT misclickTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )

    # Export targets
    install(EXPORT misclickTargets
        FILE misclickTargets.cmake
        NAMESPACE misclick::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/misclick
    )

    # Create config file
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/misclickConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_LIST_DIR}/cmake/misclickConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/misclickConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/misclick
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/misclickConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/misclickConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/misclick
    )
endif()

# Optional: Add example if examples directory exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()
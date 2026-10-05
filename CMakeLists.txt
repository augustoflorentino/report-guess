cmake_minimum_required(VERSION 3.16)
project(infer VERSION 1.0.0 DESCRIPTION "Infer the field layout of undocumented binary reports and emit a HID descriptor" LANGUAGES C)

option(INFER_BUILD_TESTS "Build tests" OFF)
option(INFER_BUILD_EXAMPLES "Build examples" OFF)
option(INFER_BUILD_FUZZ "Build libFuzzer targets" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(infer src/session.c src/stats.c src/checksum.c src/segment.c src/describe.c src/descriptor.c)
add_library(infer::infer ALIAS infer)
target_include_directories(infer PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(infer PROPERTIES
    C_STANDARD 99
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

set(PC_CFLAGS "")
if(BUILD_SHARED_LIBS)
    target_compile_definitions(infer PRIVATE INFR_BUILD_SHARED INTERFACE INFR_SHARED)
    set(PC_CFLAGS "-DINFR_SHARED")
endif()

install(TARGETS infer EXPORT inferTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT inferTargets
    NAMESPACE infer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/infer)

configure_package_config_file(cmake/inferConfig.cmake.in
    ${PROJECT_BINARY_DIR}/inferConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/infer)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/inferConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/inferConfig.cmake
    ${PROJECT_BINARY_DIR}/inferConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/infer)

configure_file(infer.pc.in ${PROJECT_BINARY_DIR}/infer.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/infer.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(INFER_BUILD_TESTS)
    enable_testing()
    add_executable(test_infer tests/test_infer.c)
    target_link_libraries(test_infer PRIVATE infer)
    add_test(NAME infer COMMAND test_infer)
    add_executable(infer_run tests/infer_run.c)
    target_link_libraries(infer_run PRIVATE infer)
    find_package(hid-parser 3.0 CONFIG QUIET)
    if(hid-parser_FOUND)
        add_executable(descriptor_check tests/descriptor_check.c)
        target_link_libraries(descriptor_check PRIVATE hid-parser::hid-parser)
        add_executable(decode_check tests/decode_check.c)
        target_link_libraries(decode_check PRIVATE infer hid-parser::hid-parser)
    endif()
    foreach(set ds4-usb:0:64 ds4-bt:0:78 ds5-usb:0:64 ds5-bt:0:78 ninja-labeled:1:10 ninja-free:1:10)
        string(REPLACE ":" ";" parts ${set})
        list(GET parts 0 name)
        list(GET parts 1 report_id)
        list(GET parts 2 report_len)
        if(hid-parser_FOUND)
            add_test(NAME hid-parser_${name}
                COMMAND descriptor_check ${PROJECT_SOURCE_DIR}/tests/data/${name}.expected ${report_id} ${report_len})
            add_test(NAME decode_check_${name}
                COMMAND decode_check ${PROJECT_SOURCE_DIR}/tests/data/${name}.samples ${report_id})
        endif()
        add_test(NAME data_${name}
            COMMAND ${CMAKE_COMMAND} -DRUN=$<TARGET_FILE:infer_run> -DREPORT_ID=${report_id}
                -DSAMPLES=${PROJECT_SOURCE_DIR}/tests/data/${name}.samples
                -DEXPECTED=${PROJECT_SOURCE_DIR}/tests/data/${name}.expected
                -P ${PROJECT_SOURCE_DIR}/tests/compare.cmake)
    endforeach()
    add_library(infer_header_check OBJECT tests/header_check.c)
    target_include_directories(infer_header_check PRIVATE include)
endif()

if(INFER_BUILD_EXAMPLES)
    add_executable(infer_basic examples/basic.c)
    target_link_libraries(infer_basic PRIVATE infer)
endif()

if(INFER_BUILD_FUZZ)
    add_executable(fuzz_reports fuzz/fuzz_reports.c)
    target_link_libraries(fuzz_reports PRIVATE infer)
    target_link_options(fuzz_reports PRIVATE -fsanitize=fuzzer)
endif()

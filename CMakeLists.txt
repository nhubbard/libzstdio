cmake_minimum_required(VERSION 3.25)

project(
	libzstdio
	VERSION 0.1.0
	DESCRIPTION "Linux FUSE passthrough and Zstd codec"
	LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZSTD REQUIRED IMPORTED_TARGET libzstd)

add_library(zstdio_codec STATIC src/codec.c src/decoded_fd.c)
target_compile_definitions(zstdio_codec PRIVATE _GNU_SOURCE)
target_include_directories(zstdio_codec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(zstdio_codec PRIVATE PkgConfig::LIBZSTD)

include(CTest)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(FUSE3 IMPORTED_TARGET fuse3)
    if(FUSE3_FOUND)
        add_executable(zstdio_fuse src/fuse_passthrough.c src/fuse_storage.c)
        target_compile_definitions(zstdio_fuse PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
        target_compile_options(zstdio_fuse PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(zstdio_fuse PRIVATE PkgConfig::FUSE3 PkgConfig::LIBZSTD)

        if(BUILD_TESTING)
            add_executable(zstdio_fuse_probe tests/fuse_probe.c)
            target_compile_definitions(zstdio_fuse_probe PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
            target_compile_options(zstdio_fuse_probe PRIVATE -Wall -Wextra -Wpedantic)
            add_test(NAME fuse_probe_native
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuse_passthrough.sh"
                        native $<TARGET_FILE:zstdio_fuse_probe>)
            add_test(NAME fuse_passthrough
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuse_passthrough.sh"
                        mounted $<TARGET_FILE:zstdio_fuse_probe> $<TARGET_FILE:zstdio_fuse>)
            set_tests_properties(fuse_passthrough PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
            find_program(ZSTD_TRAINER zstd)
            if(ZSTD_TRAINER)
                add_test(NAME fuse_dictionary
                    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuse_passthrough.sh"
                            dictionary $<TARGET_FILE:zstdio_fuse_probe>
                            $<TARGET_FILE:zstdio_fuse> "${ZSTD_TRAINER}")
                set_tests_properties(fuse_dictionary PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
            endif()
        endif()
    endif()
endif()

if(BUILD_TESTING)
    add_executable(zstdio_codec_probe tests/codec_probe.c)
    target_link_libraries(zstdio_codec_probe PRIVATE zstdio_codec)
    add_test(NAME codec_roundtrip COMMAND zstdio_codec_probe)

    add_executable(zstdio_shadow_fd_probe tests/shadow_fd_probe.c)
    target_link_libraries(zstdio_shadow_fd_probe PRIVATE zstdio_codec)
endif()

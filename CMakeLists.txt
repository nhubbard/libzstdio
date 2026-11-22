cmake_minimum_required(VERSION 3.25)

project(
	libzstdio
	VERSION 0.1.0
	DESCRIPTION "Transparent Zstd filesystem interposition"
	LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(zstdio SHARED
	src/interpose.c
    src/descriptor_io.c
    src/stdio_io.c
    src/mapping_io.c
    src/trace_io.c
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"
   AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM)
    target_sources(zstdio PRIVATE src/variadic_x86_64.S)
    target_compile_definitions(zstdio PRIVATE ZSTDIO_X86_64_VARIADIC)
endif()

set_target_properties(zstdio PROPERTIES
	OUTPUT_NAME zstdio
	VERSION ${PROJECT_VERSION}
	SOVERSION 0
)

target_include_directories(zstdio PRIVATE
	${PROJECT_SOURCE_DIR}/include
)

target_compile_definitions(zstdio PRIVATE
	_GNU_SOURCE
)

target_compile_options(zstdio PRIVATE
    $<$<COMPILE_LANGUAGE:C>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wcast-align
        -Wpointer-arith
        -Wundef
        -Werror
    >
)

target_link_libraries(zstdio PRIVATE
	${CMAKE_DL_LIBS}
)

include(CTest)

if(BUILD_TESTING)
	add_executable(zstdio_probe tests/probe.c)

	add_test(
		NAME preload_smoke_test
		COMMAND
			${CMAKE_COMMAND} -E env
			"LD_PRELOAD=$<TARGET_FILE:zstdio>"
			$<TARGET_FILE:zstdio_probe>
	)

    add_executable(zstdio_openat_probe tests/openat_probe.c)
    add_test(NAME openat_native COMMAND zstdio_openat_probe)
    add_test(
        NAME openat_preload
        COMMAND
            ${CMAKE_COMMAND} -E env
            "LD_PRELOAD=$<TARGET_FILE:zstdio>"
            $<TARGET_FILE:zstdio_openat_probe>
    )

    find_package(Threads REQUIRED)
	add_executable(zstdio_descriptor_race tests/descriptor_race.c)
	target_link_libraries(zstdio_descriptor_race PRIVATE Threads::Threads)

	add_test(NAME descriptor_race_native COMMAND zstdio_descriptor_race)
	add_test(
		NAME descriptor_race_preload
		COMMAND
			${CMAKE_COMMAND} -E env
			"LD_PRELOAD=$<TARGET_FILE:zstdio>"
			$<TARGET_FILE:zstdio_descriptor_race>
	)
	set_tests_properties(
		descriptor_race_native descriptor_race_preload
		PROPERTIES TIMEOUT 10
	)

    add_executable(zstdio_stdio_probe tests/stdio_probe.c)
	add_test(NAME stdio_native COMMAND zstdio_stdio_probe)
	add_test(
		NAME stdio_preload
		COMMAND
			${CMAKE_COMMAND} -E env
			"LD_PRELOAD=$<TARGET_FILE:zstdio>"
			$<TARGET_FILE:zstdio_stdio_probe>
	)

    add_executable(zstdio_seek_probe tests/seek_probe.c)
	add_test(NAME seek_native COMMAND zstdio_seek_probe)
	add_test(
		NAME seek_preload
		COMMAND
			${CMAKE_COMMAND} -E env
			"LD_PRELOAD=$<TARGET_FILE:zstdio>"
			$<TARGET_FILE:zstdio_seek_probe>
	)

    add_executable(zstdio_mapping_probe tests/mapping_probe.c)
	add_test(NAME mapping_native COMMAND zstdio_mapping_probe)
	add_test(
		NAME mapping_preload
		COMMAND
			${CMAKE_COMMAND} -E env
			"LD_PRELOAD=$<TARGET_FILE:zstdio>"
			$<TARGET_FILE:zstdio_mapping_probe>
	)
endif()

option(
    ZSTDIO_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UBSan"
    OFF
)

if(ZSTDIO_ENABLE_SANITIZERS)
    target_compile_options(zstdio PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )

    target_link_options(zstdio PRIVATE
        -fsanitize=address,undefined
    )
endif()
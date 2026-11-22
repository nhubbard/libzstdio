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
)

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
	-Wall
	-Wextra
	-Wpedantic
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
endif()

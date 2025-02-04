cmake_minimum_required(VERSION 3.21)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

macro(find_dependency_path DEPENDENCY FILE)
	# searches extern for dependencies and if not checks the environment variable
	if(NOT ${DEPENDENCY} STREQUAL "")
		# Check extern
		message(
			STATUS
			"Searching for ${DEPENDENCY} using file ${FILE}"
		)
		find_path("${DEPENDENCY}Path"
			${FILE}
			PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../extern/${DEPENDENCY}")

		if("${${DEPENDENCY}Path}" STREQUAL "${DEPENDENCY}Path-NOTFOUND")
			# Check path
			message(
				STATUS
				"Getting environment for ${DEPENDENCY}Path: $ENV{${DEPENDENCY}Path}"
			)
			set("${DEPENDENCY}Path" "$ENV{${DEPENDENCY}Path}")
		endif()

		message(
			STATUS
			"Found ${DEPENDENCY} in ${${DEPENDENCY}Path}; adding"
		)
		add_subdirectory("${${DEPENDENCY}Path}" ${DEPENDENCY})
	endif()
endmacro()

# info
project(
	NAF-Common
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)

# set CommonLib info
set(COMMON_LIB_TYPE "SF" CACHE STRING "Choose the type of CommonLib: SF or F4")
set_property(CACHE COMMON_LIB_TYPE PROPERTY STRINGS SF F4)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/config.h
)

# dependencies
add_library(CommonLibInterface INTERFACE)

if(COMMON_LIB_TYPE STREQUAL "F4")
    add_subdirectory("../extern/CommonLibF4/" CommonLibF4)
    target_link_libraries(CommonLibInterface INTERFACE CommonLibF4::CommonLibF4)
elseif(COMMON_LIB_TYPE STREQUAL "SF")
    add_subdirectory("../extern/CommonLibSF/" CommonLibSF)
    target_link_libraries(CommonLibInterface INTERFACE CommonLibSF::CommonLibSF)
else()
    message(FATAL_ERROR "Invalid COMMON_LIB_TYPE specified")
endif()

find_library(DETOURS_LIBRARY detours REQUIRED)
find_package(libzippp CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
add_subdirectory("../extern/ozz-animation/" ozz-animation)
add_subdirectory("../extern/fastgltf/" fastgltf)

# glob source files
file(GLOB_RECURSE SRC_FILES src/*.cpp)

# cmake target
# runtime
add_library(
	${PROJECT_NAME}
	STATIC
		${SRC_FILES}
		.clang-format
		vcpkg.json)

# include dir
target_include_directories(
	${PROJECT_NAME}
	PUBLIC
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

# linkage
target_link_libraries(
	${PROJECT_NAME} 
	PUBLIC
		simdjson::simdjson
		fastgltf::fastgltf
		libzippp::libzippp
		ozz_animation
		ozz_animation_offline
		CommonLibInterface
		${DETOURS_LIBRARY}
)

# compiler def
if (MSVC)
	add_compile_definitions(_UNICODE)

	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			/MP
			/await
			/W0
			/WX
			/permissive-
			/utf-8
			/Zc:alignedNew
			/Zc:auto
			/Zc:__cplusplus
			/Zc:externC
			/Zc:externConstexpr
			/Zc:forScope
			/Zc:hiddenFriend
			/Zc:implicitNoexcept
			/Zc:lambda
			/Zc:noexceptTypes
			/Zc:preprocessor
			/Zc:referenceBinding
			/Zc:rvalueCast
			/Zc:sizedDealloc
			/Zc:strictStrings
			/Zc:ternary
			/Zc:threadSafeInit
			/Zc:trigraphs
			/Zc:wchar_t
			/wd4200 # nonstandard extension used : zero-sized array in struct/union
			/FI${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h
	)
endif()

# PCH
target_precompile_headers(
	${PROJECT_NAME}
	PUBLIC
		src/PCH.h
)
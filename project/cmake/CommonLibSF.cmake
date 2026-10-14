# libxse/commonlibsf only ships an xmake build. This mirrors its xmake.lua target so it can be built from CMake.
# commonlib-shared (its submodule) already provides a CMakeLists.txt.

set(COMMONLIBSF_DIR "${CMAKE_CURRENT_LIST_DIR}/../../extern/CommonLibSF")

if(NOT EXISTS "${COMMONLIBSF_DIR}/lib/commonlib-shared/CMakeLists.txt")
	message(FATAL_ERROR "commonlib-shared is missing; run `git submodule update --init --recursive`.")
endif()

add_subdirectory("${COMMONLIBSF_DIR}/lib/commonlib-shared" commonlib-shared)

file(GLOB_RECURSE COMMONLIBSF_SOURCES CONFIGURE_DEPENDS "${COMMONLIBSF_DIR}/src/*.cpp")

add_library(CommonLibSF STATIC ${COMMONLIBSF_SOURCES})
add_library(CommonLibSF::CommonLibSF ALIAS CommonLibSF)

target_include_directories(CommonLibSF PUBLIC "${COMMONLIBSF_DIR}/include")
target_link_libraries(CommonLibSF PUBLIC commonlib-shared::commonlib-shared)
target_compile_features(CommonLibSF PUBLIC cxx_std_23)

if(MSVC)
	# commonlib-shared applies these to itself in xmake, but its CMake build leaves them out.
	target_compile_options(
		commonlib-shared
		PUBLIC
			/EHsc
			/permissive-
			/utf-8
			/Zc:preprocessor
			/bigobj
	)
endif()

# CommonLibSF's headers rely on this being force-included, as xmake's set_pcxxheader does.
target_precompile_headers(CommonLibSF PUBLIC "${COMMONLIBSF_DIR}/include/SFSE/Impl/PCH.h")

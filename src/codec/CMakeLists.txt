cmake_minimum_required(VERSION 3.8)
cmake_policy(SET CMP0015 NEW)
cmake_policy(SET CMP0068 NEW)


### project definition

project(palladio_codec CXX)

include(../platform.cmake)


### build target

add_library(${PROJECT_NAME} SHARED
		CodecMain.cpp
		encoder/HoudiniEncoder.cpp
		encoder/HoudiniCallbacks.h)

if(PLD_LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE
			-std=c++11 -march=nocona
			-fvisibility=hidden -fvisibility-inlines-hidden)

	if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
		target_compile_options(${PROJECT_NAME} PRIVATE -O3 -flto)
		target_compile_definitions(${PROJECT_NAME} PRIVATE -DNDEBUG)
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
		target_compile_options(${PROJECT_NAME} PRIVATE -O3 -ggdb -pg)
		target_compile_definitions(${PROJECT_NAME} PRIVATE -DNDEBUG)
	elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
		target_compile_options(${PROJECT_NAME} PRIVATE -O0 -ggdb -pg)
		target_compile_definitions(${PROJECT_NAME} PRIVATE -DDEBUG)
	endif()

elseif(PLD_MACOS)
	target_compile_options(${PROJECT_NAME} PRIVATE
			-std=c++14
			#-fvisibility=hidden -fvisibility-inlines-hidden
			)

elseif(PLD_WINDOWS)
	target_compile_options(${PROJECT_NAME} PRIVATE
			/bigobj /GR /EHsc /MD)
endif()


### dependencies

# TODO: try to avoid the code copies below

### setup conan

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
    message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
    file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/v0.11/conan.cmake"
            "${CMAKE_BINARY_DIR}/conan.cmake")
endif()

include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_cmake_run(CONANFILE conanfile.py BASIC_SETUP CMAKE_TARGETS BUILD missing)


### PRT dependency

function(pld_add_dependency_prt TGT)
    find_package(prt CONFIG REQUIRED)
    target_compile_definitions(${TGT} PRIVATE -DPRT_VERSION_MAJOR=${PRT_VERSION_MAJOR} -DPRT_VERSION_MINOR=${PRT_VERSION_MINOR})
    target_include_directories(${TGT} PRIVATE ${PRT_INCLUDE_PATH})
    target_link_libraries(${TGT} PRIVATE ${PRT_LIBRARY})
endfunction()

pld_add_dependency_prt(${PROJECT_NAME})


### install target

# TODO: do codec installation in palladio install step

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION prtlib
	LIBRARY DESTINATION prtlib)

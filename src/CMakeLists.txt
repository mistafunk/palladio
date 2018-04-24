cmake_minimum_required(VERSION 3.8)
cmake_policy(SET CMP0068 NEW)


### project definition

project(palladio_root CXX)

include(${CMAKE_CURRENT_LIST_DIR}/platform.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/dependencies.cmake)


### versioning

set(PLD_VERSION_MAJOR 1)
set(PLD_VERSION_MINOR 1)
set(PLD_VERSION_PATCH 2)
if(NOT PLD_VERSION_BUILD)
    set(PLD_VERSION_BUILD DEV)
endif()

# we use semver.org, Semantic Versioning 2.0.0
# i.e. <major>.<minor>.<patch>+b<buildnr>.hdn<houdini ver>.prt<prt ver>
set(PLD_VERSION "${PLD_VERSION_MAJOR}.${PLD_VERSION_MINOR}.${PLD_VERSION_PATCH}\
+b${PLD_VERSION_BUILD}\
.hdn${Houdini_VERSION_MAJOR}-${Houdini_VERSION_MINOR}-${Houdini_VERSION_PATCH}\
.prt${PRT_VERSION_MAJOR}-${PRT_VERSION_MINOR}-${PRT_VERSION_MICRO}")
message(STATUS "Using PLD_VERSION = ${PLD_VERSION}")


### setup installation paths

if (NOT HOUDINI_DSO_PATH)
	if(PLD_WINDOWS)
		set(HOUDINI_DSO_PATH "$ENV{HOMEDRIVE}$ENV{HOMEPATH}/Documents/houdini${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}/dso")
	elseif(PLD_LINUX)
		set(HOUDINI_DSO_PATH "$ENV{HOME}/houdini${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}/dso")
	elseif(PLD_MACOS)
		set(HOUDINI_DSO_PATH "/Users/Shared/houdini/${Houdini_VERSION_MAJOR}.${Houdini_VERSION_MINOR}/dso")
	endif()
endif()
message(STATUS "HOUDINI_DSO_PATH = ${HOUDINI_DSO_PATH}")

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set (CMAKE_INSTALL_PREFIX "${HOUDINI_DSO_PATH}" CACHE PATH "default install path" FORCE )
	message(STATUS "Set default install prefix: ${CMAKE_INSTALL_PREFIX}")
endif()


### setup codec dependency target

# we use ExternalProject to emulate a standalone cmake run for codec as it potentially needs a different compiler

include(ExternalProject)
ExternalProject_Add(codec
	SOURCE_DIR ${CMAKE_SOURCE_DIR}/codec
    PREFIX ${CMAKE_BINARY_DIR}/codec
	CMAKE_ARGS
		-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
		-DCMAKE_OSX_SYSROOT=/Applications/Xcode833.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
		-DCMAKE_CXX_COMPILER=/Applications/Xcode833.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++
		-DCMAKE_C_COMPILER=/Applications/Xcode833.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang
		-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
)
#configure_file(conanfile.py )


### setup palladio target

add_subdirectory(palladio)
add_dependencies(palladio codec)
#add_subdirectory(test EXCLUDE_FROM_ALL)


### setup package target

set(CPACK_PACKAGE_NAME                "palladio")
set(CPACK_PACKAGE_VENDOR              "Esri R&D Zurich / VRBN")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Houdini Plugin based on Esri CityEngine")
set(CPACK_PACKAGE_VERSION_MAJOR       ${PLD_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR       ${PLD_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH       ${PLD_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "palladio-${PLD_VERSION}")

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(PLD_PKG_OS "windows")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(PLD_PKG_OS "linux")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	set(PLD_PKG_OS "macos")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PLD_VERSION}-${PLD_PKG_OS}")

if(PLD_WINDOWS)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()

include(CPack)

# not strictly necessary, useful for IDEs like CLion without direct support to run "make package"
add_custom_target(palladio_package
	${CMAKE_MAKE_PROGRAM} package
    DEPENDS palladio
    COMMENT "Packaging Palladio")


### setup test target

enable_testing ()
add_test (NAME palladio_test COMMAND palladio_test)
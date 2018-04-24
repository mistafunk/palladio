### platform configuration

message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(PLD_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(PLD_LINUX 1)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(PLD_MACOS 1)
endif()


### toolchain configuration

function(add_toolchain_definition TGT)
    if(PLD_WINDOWS)
        target_compile_definitions(${TGT} PRIVATE -DPLD_WINDOWS=1 -DPLD_TC_VC=1)
    elseif(PLD_LINUX)
        target_compile_definitions(${TGT} PRIVATE -DPLD_LINUX=1 -DPLD_TC_GCC=1)
    elseif(PLD_MACOS)
        target_compile_definitions(${TGT} PRIVATE -DPLD_MACOS=1 -DPLD_TC_CLANG=1)
    endif()
endfunction()

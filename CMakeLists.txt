cmake_minimum_required(VERSION 3.29)
project(deshortify_gui)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# SDL
include_directories(include)
add_compile_options(-Wno-strict-prototypes -Wno-unused-but-set-variable -pedantic -Wno-ignored-attributes -Wstrict-aliasing -Wstrict-overflow -g)
set(LIB_DIR lib)
if (WIN32)
    find_library(SDL2_LIB SDL2 PATHS ${LIB_DIR}/win32 NO_DEFAULT_PATH)
    find_library(SDL2MAIN_LIB SDL2main PATHS ${LIB_DIR}/win32 NO_DEFAULT_PATH)
    find_library(SDL2_TFF_LIB SDL2_ttf PATHS ${LIB_DIR}/win32 NO_DEFAULT_PATH)
    set(MINGW32_LIB mingw32)
    # glob all the dlls
    file(GLOB DLLS ${LIB_DIR}/win32/*.dll)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
endif()
add_executable(deshortify_gui main.cpp)
target_link_libraries(deshortify_gui PRIVATE ${MINGW32_LIB} ${SDL2MAIN_LIB} ${SDL2_LIB} ${SDL2_TFF_LIB})

# Copy *.dll / *.so / *.dylib files
if(NOT "${DLLS}" STREQUAL "")
    add_custom_command(
            TARGET deshortify_gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${DLLS} $<TARGET_FILE_DIR:deshortify_gui>
    )
endif()

add_custom_command(
        TARGET deshortify_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin $<TARGET_FILE_DIR:deshortify_gui>/bin
)

add_custom_command(
        TARGET deshortify_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:deshortify_gui>/assets
)
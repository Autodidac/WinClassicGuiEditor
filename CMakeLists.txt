cmake_minimum_required(VERSION 3.29)
project(Win32UIEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Win32UIEditor
    src/main.cpp
)

# C++23 module
target_sources(Win32UIEditor
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            src/win32_ui_editor.ixx
)

if (MSVC)
    target_compile_options(Win32UIEditor PRIVATE /permissive- /Zc:preprocessor /EHsc)
    target_link_libraries(Win32UIEditor PRIVATE Comctl32)
    set_property(TARGET Win32UIEditor PROPERTY VS_GLOBAL_EnableCppModules "true")
endif()

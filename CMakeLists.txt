cmake_minimum_required(VERSION 3.29)

set(APP_VERSION 0.3.0)
project(Win32UIEditor VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Win32UIEditor
    WinClassicGuiEditor/src/main.cpp
)
set_target_properties(Win32UIEditor PROPERTIES WIN32_EXECUTABLE TRUE)

# C++23 module
target_sources(Win32UIEditor
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            WinClassicGuiEditor/src/win32_ui_editor.ixx
            WinClassicGuiEditor/src/win32_ui_editor.model.ixx
            WinClassicGuiEditor/src/win32_ui_editor.importparser.ixx
)

if (MSVC)
    target_compile_definitions(Win32UIEditor PRIVATE UNICODE _UNICODE)
    target_compile_options(Win32UIEditor PRIVATE /permissive- /Zc:preprocessor /EHsc)
    target_link_libraries(Win32UIEditor PRIVATE Comctl32)
    set_property(TARGET Win32UIEditor PROPERTY VS_GLOBAL_EnableCppModules "true")
endif()

enable_testing()

add_executable(ImportParserTests
    WinClassicGuiEditor/src/importparser_tests.cpp
)

target_sources(ImportParserTests
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            WinClassicGuiEditor/src/win32_ui_editor.model.ixx
            WinClassicGuiEditor/src/win32_ui_editor.importparser.ixx
)

if (MSVC)
    target_compile_definitions(ImportParserTests PRIVATE UNICODE _UNICODE)
    target_compile_options(ImportParserTests PRIVATE /permissive- /Zc:preprocessor /EHsc)
    set_property(TARGET ImportParserTests PROPERTY VS_GLOBAL_EnableCppModules "true")
endif()

add_test(NAME ImportParserTests COMMAND ImportParserTests)

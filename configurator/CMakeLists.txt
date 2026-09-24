cmake_minimum_required(VERSION 3.16)
project(AcocConfigurator LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(APP_SOURCES
    src/main.cpp
    src/generator.cpp
    src/roster.cpp
    src/zipfile.cpp
)

# windres 默认用 popen 读预处理器输出，部分 MinGW 版本上会失败；
# 这里改用临时文件方式，并显式给出临时目录，免得依赖调用者的 TMP。
set(RES_OBJ "${CMAKE_BINARY_DIR}/acoc_res.obj")
set(RES_TMP "${CMAKE_BINARY_DIR}/res_tmp")
file(MAKE_DIRECTORY "${RES_TMP}")
add_custom_command(
    OUTPUT "${RES_OBJ}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RES_TMP}"
    COMMAND ${CMAKE_COMMAND} -E env "TMP=${RES_TMP}" "TEMP=${RES_TMP}"
            "${CMAKE_RC_COMPILER}" --use-temp-file
            -I "${CMAKE_CURRENT_SOURCE_DIR}/res"
            -i "${CMAKE_CURRENT_SOURCE_DIR}/res/app.rc"
            -O coff -o "${RES_OBJ}"
    DEPENDS res/app.rc res/icon.ico res/template.html res/app.manifest
    COMMENT "编译资源 icon / 模板 / 清单"
    VERBATIM
)
set_source_files_properties("${RES_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

add_executable(acoc_configurator WIN32 ${APP_SOURCES} "${RES_OBJ}")

target_include_directories(acoc_configurator PRIVATE src)

target_compile_definitions(acoc_configurator PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601   # Windows 7
    WINVER=0x0601
)

# 源码是 UTF-8，窄字符串也按 UTF-8 输出，运行时统一转 UTF-16
target_compile_options(acoc_configurator PRIVATE
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Os
)

target_link_libraries(acoc_configurator PRIVATE
    comctl32 comdlg32 gdi32 user32 shell32
)

if(MINGW)
    # 静态链接 gcc / libstdc++，运行时只依赖系统 DLL
    # -municode 让入口点变成 wWinMain（源码用宽字符入口）
    target_link_options(acoc_configurator PRIVATE
        -municode
        -static -static-libgcc -static-libstdc++
        -Wl,--nxcompat -Wl,--dynamicbase
    )
endif()

set_target_properties(acoc_configurator PROPERTIES
    OUTPUT_NAME "ACOCConfigurator"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

cmake_minimum_required(VERSION 3.16)
project(AcocSetup LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 安装程序把配置器本体当资源打包进去，所以要先备好这份拷贝
set(APP_EXE "${CMAKE_CURRENT_SOURCE_DIR}/../build/bin/ACOCConfigurator.exe")
set(PAYLOAD "${CMAKE_CURRENT_SOURCE_DIR}/res/payload.exe")
if(NOT EXISTS "${APP_EXE}")
    message(FATAL_ERROR "找不到 ${APP_EXE}，请先在上级目录构建配置器本体。")
endif()

add_custom_command(
    OUTPUT "${PAYLOAD}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${APP_EXE}" "${PAYLOAD}"
    DEPENDS "${APP_EXE}"
    COMMENT "拷入待安装的程序本体"
    VERBATIM
)
add_custom_target(payload DEPENDS "${PAYLOAD}")

# windres 在这台机器上的 popen 预处理路径是坏的，显式改用临时文件
set(RES_OBJ "${CMAKE_BINARY_DIR}/setup_res.obj")
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
    DEPENDS res/app.rc res/icon.ico res/app.manifest "${PAYLOAD}"
    COMMENT "编译安装程序资源"
    VERBATIM
)
set_source_files_properties("${RES_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

add_executable(acoc_setup WIN32 src/main.cpp "${RES_OBJ}")
add_dependencies(acoc_setup payload)

target_compile_definitions(acoc_setup PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 WINVER=0x0601
)
target_compile_options(acoc_setup PRIVATE
    -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra
    -Wno-unused-parameter -Os
)

# shlobj / ole32 用来建快捷方式和访问已知文件夹
target_link_libraries(acoc_setup PRIVATE
    comctl32 shell32 ole32 uuid user32 gdi32
)

if(MINGW)
    target_link_options(acoc_setup PRIVATE
        -municode
        -static -static-libgcc -static-libstdc++
        -Wl,--nxcompat -Wl,--dynamicbase
    )
endif()

set_target_properties(acoc_setup PROPERTIES
    OUTPUT_NAME "ACOCConfiguratorSetup"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

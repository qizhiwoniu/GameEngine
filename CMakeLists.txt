cmake_minimum_required(VERSION 3.16)
project(GameEngine) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")                                   # Windows API 使用宽字符版本
    add_compile_options(/utf-8)                                                 # 源文件按 UTF-8 解析
endif()

set(CMAKE_AUTORCC ON)   # .qrc 资源编译进 exe

# ---- Qt6 ------------------------------------------------------------------
# 默认用本机 Qt 安装路径；换机器时 cmake -DCMAKE_PREFIX_PATH=<Qt6>/msvc2022_64 覆盖即可
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/QT/6.10.0/msvc2022_64" CACHE STRING "Qt6 安装路径")
endif()
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()                                                     # 打开 AUTOMOC / AUTOUIC / AUTORCC

# ---- SDL3 ------------------------------------------------------------------
# 本仓库自带的 VC 开发包（ThirdParty/SDL3-3.4.2），换机器只需要拷这个目录
set(SDL3_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/SDL3-3.4.2/cmake" CACHE STRING "SDL3 cmake 配置目录")
find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
    
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.qrc"
    "${CMAKE_SOURCE_DIR}/Assets/*.qrc"
    "${CMAKE_SOURCE_DIR}/logo.rc")

# ON 使用 Win32 GUI 子系统, OFF 使用控制台子系统
option(USE_WIN32 "Use Win32 GUI subsystem" ON)

#if(USE_WIN32)
    add_executable(GameEngine WIN32 ${SRC})
    target_sources(GameEngine PRIVATE "${CMAKE_SOURCE_DIR}/Assets/app.qrc")   # 图标等 Qt 资源
    target_compile_definitions(GameEngine PRIVATE USE_WIN32)
#else()
#    add_executable(GameEngine ${SRC})
#endif()  

target_include_directories(GameEngine PRIVATE
    "${CMAKE_SOURCE_DIR}/Src"
    "${CMAKE_SOURCE_DIR}/Src/App"
    "${CMAKE_SOURCE_DIR}/Src/Core"
    "${CMAKE_SOURCE_DIR}/Src/Editor"
    "${CMAKE_SOURCE_DIR}/Src/NotifyIcon"
    "${CMAKE_SOURCE_DIR}/Src/UI"
    "${CMAKE_SOURCE_DIR}/ThirdParty"
)

target_link_libraries(GameEngine PRIVATE
    dwmapi.lib
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    Qt6::Widgets
    SDL3::SDL3
)

# SDL3.dll 拷到 exe 旁边，双击就能跑
if(WIN32)
    add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/ThirdParty/SDL3-3.4.2/lib/x64/SDL3.dll"
                "$<TARGET_FILE_DIR:GameEngine>"
        COMMENT "Copy SDL3.dll"
    )
endif()

# 把 Qt 运行时（Qt6Core/Gui/Widgets + platforms 插件）拷到 exe 旁边，双击就能跑
if(WIN32)
    add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler
                --no-opengl-sw "$<TARGET_FILE:GameEngine>"
        COMMENT "windeployqt: 部署 Qt6 运行时"
    )
endif()

set_target_properties(GameEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/GameEngine"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/GameEngine"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/GameEngine"
    VS_DEBUGGER_WORKING_DIRECTORY    "${CMAKE_BINARY_DIR}/GameEngine"
)  

if(EXISTS "${CMAKE_SOURCE_DIR}/Assets")
    add_custom_command(TARGET GameEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Assets"
                "$<TARGET_FILE_DIR:GameEngine>"
        COMMENT "Copy assets contents for GameEngine"
    )
endif()

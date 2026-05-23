cmake_minimum_required(VERSION 3.16)
project(RegexBlocks LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# 三平台 Qt6 自动检测 (用户也可用 -DCMAKE_PREFIX_PATH=... 显式指定)
# ============================================================================
if(NOT CMAKE_PREFIX_PATH AND NOT Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
    if(APPLE)
        # 优先 Homebrew 软链, 其次 Cellar
        foreach(_p "/opt/homebrew/opt/qt" "/opt/homebrew/opt/qtbase"
                   "/usr/local/opt/qt" "/usr/local/opt/qtbase")
            if(EXISTS "${_p}/lib/cmake/Qt6")
                list(APPEND CMAKE_PREFIX_PATH "${_p}")
                break()
            endif()
        endforeach()
        if(NOT CMAKE_PREFIX_PATH)
            file(GLOB _qtbase_dirs "/opt/homebrew/Cellar/qtbase/*"
                                   "/usr/local/Cellar/qtbase/*")
            if(_qtbase_dirs)
                list(SORT _qtbase_dirs)
                list(REVERSE _qtbase_dirs)
                list(GET _qtbase_dirs 0 _latest)
                list(APPEND CMAKE_PREFIX_PATH "${_latest}")
            endif()
        endif()
    elseif(WIN32)
        # 常见 Qt 在线安装器路径
        foreach(_v 6.8.0 6.7.0 6.6.0 6.5.0)
            foreach(_c msvc2022_64 msvc2019_64 mingw_64)
                if(EXISTS "C:/Qt/${_v}/${_c}/lib/cmake/Qt6")
                    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/${_v}/${_c}")
                endif()
            endforeach()
        endforeach()
    elseif(UNIX)
        # 系统包优先, 其次 Qt 在线安装器
        foreach(_v 6.8.0 6.7.0 6.6.0 6.5.0)
            foreach(_c gcc_64 linux-g++)
                if(EXISTS "$ENV{HOME}/Qt/${_v}/${_c}/lib/cmake/Qt6")
                    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/${_v}/${_c}")
                endif()
            endforeach()
        endforeach()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ============================================================================
# 源文件
# ============================================================================
set(SOURCES
    main.cpp
    src/ui/mainwindow.cpp
    src/ui/blockpalette.cpp
    src/ui/blockcanvas.cpp
    src/ui/testpanel.cpp
    src/ui/blockeditor.cpp
    src/ui/theme.cpp
    src/core/block.cpp
    src/core/regexcompiler.cpp
    src/core/regexparser.cpp
    src/core/commands.cpp
    src/core/templates.cpp
    src/graphics/blockgraphicsitem.cpp
)

set(HEADERS
    src/ui/mainwindow.h
    src/ui/blockpalette.h
    src/ui/blockcanvas.h
    src/ui/testpanel.h
    src/ui/blockeditor.h
    src/ui/theme.h
    src/core/block.h
    src/core/regexcompiler.h
    src/core/regexparser.h
    src/core/commands.h
    src/core/templates.h
    src/graphics/blockgraphicsitem.h
    src/util/fonts.h
    src/util/appicon.h
)

# ============================================================================
# 资源 (图标等)
# ============================================================================
set(RESOURCES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/resources.qrc")
    list(APPEND RESOURCES "resources/resources.qrc")
endif()

# ============================================================================
# 平台特定可执行文件
# ============================================================================
if(WIN32)
    # Windows: WIN32_EXECUTABLE 抑制控制台窗口
    set(_app_icon_rc "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/app_icon.rc")
        set(_app_icon_rc "resources/app_icon.rc")
    endif()
    add_executable(RegexBlocks WIN32
                   ${SOURCES} ${HEADERS} ${RESOURCES} ${_app_icon_rc})
elseif(APPLE)
    # macOS: MACOSX_BUNDLE 生成 .app
    set(_app_icon_mac "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/app_icon.icns")
        set(_app_icon_mac "resources/app_icon.icns")
        set_source_files_properties(${_app_icon_mac}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    endif()
    add_executable(RegexBlocks MACOSX_BUNDLE
                   ${SOURCES} ${HEADERS} ${RESOURCES} ${_app_icon_mac})
    set_target_properties(RegexBlocks PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME       "RegexBlocks"
        MACOSX_BUNDLE_BUNDLE_VERSION    "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER    "com.course.RegexBlocks"
        MACOSX_BUNDLE_ICON_FILE         "app_icon.icns"
    )
else()
    # Linux 与其他 Unix
    add_executable(RegexBlocks ${SOURCES} ${HEADERS} ${RESOURCES})
endif()

target_link_libraries(RegexBlocks PRIVATE Qt6::Widgets)
target_include_directories(RegexBlocks PRIVATE src)

# ============================================================================
# 测试 (可选): 反向解析 + 编译往返一致性
#   配置: cmake -DREGEXBLOCKS_BUILD_TESTS=ON ..
#   运行: ./regexblocks_tests   (退出码 0 = 全过, 非零 = 有失败)
# ============================================================================
option(REGEXBLOCKS_BUILD_TESTS "Build round-trip tests" OFF)
if(REGEXBLOCKS_BUILD_TESTS)
    # Block::color() 返回 QColor (来自 Qt Gui), 所以测试也需要 Gui
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_executable(regexblocks_tests
        tests/regexblocks_tests.cpp
        src/core/block.cpp
        src/core/regexcompiler.cpp
        src/core/regexparser.cpp
        src/core/templates.cpp
    )
    target_include_directories(regexblocks_tests PRIVATE src)
    target_link_libraries(regexblocks_tests PRIVATE Qt6::Core Qt6::Gui)
    enable_testing()
    add_test(NAME RegexBlocksRoundTrip COMMAND regexblocks_tests)
endif()

# ============================================================================
# Linux 安装规则 (FHS 标准)
# ============================================================================
if(UNIX AND NOT APPLE)
    install(TARGETS RegexBlocks RUNTIME DESTINATION bin)
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/regexblocks.desktop")
        install(FILES resources/regexblocks.desktop
                DESTINATION share/applications)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/app_icon.png")
        install(FILES resources/app_icon.png
                DESTINATION share/icons/hicolor/512x512/apps
                RENAME regexblocks.png)
    endif()
endif()

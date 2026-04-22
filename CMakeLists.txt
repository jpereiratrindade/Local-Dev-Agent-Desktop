cmake_minimum_required(VERSION 3.20)
project(AgentGUI VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
set(OpenGL_GL_PREFERENCE LEGACY)
if (EXISTS "/usr/lib/x86_64-linux-gnu/libGL.so" AND NOT EXISTS "/usr/lib64/libGL.so")
    set(OPENGL_gl_LIBRARY "/usr/lib/x86_64-linux-gnu/libGL.so" CACHE FILEPATH "OpenGL legacy GL library" FORCE)
endif()
if (EXISTS "/usr/lib/x86_64-linux-gnu/libOpenGL.so" AND NOT EXISTS "/usr/lib64/libOpenGL.so")
    set(OPENGL_opengl_LIBRARY "/usr/lib/x86_64-linux-gnu/libOpenGL.so" CACHE FILEPATH "OpenGL library" FORCE)
endif()
if (EXISTS "/usr/lib/x86_64-linux-gnu/libGLX.so" AND NOT EXISTS "/usr/lib64/libGLX.so")
    set(OPENGL_glx_LIBRARY "/usr/lib/x86_64-linux-gnu/libGLX.so" CACHE FILEPATH "OpenGL GLX library" FORCE)
endif()
find_package(OpenGL REQUIRED)

include_directories(
    include
    third_party/vendor
    third_party/vendor/imgui
    third_party/vendor/imgui/backends
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)

set(SOURCES
    src/main.cpp
    src/OllamaClient.cpp
    src/AgentUI.cpp
    src/AgentUI_Chat.cpp
    src/AgentUI_Explorer.cpp
    src/AgentUI_Modals.cpp
    src/AgentUI_Stats.cpp
    src/AgentUI_Persistence.cpp
    src/ToolRegistry.cpp
    src/EvidenceModel.cpp
    src/Orchestrator.cpp
    src/NativeTools.cpp
)

set(IMGUI_DIR third_party/vendor/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    third_party/vendor/TextEditor.cpp
)

add_executable(AgentGUI ${SOURCES} ${IMGUI_SOURCES})
target_compile_definitions(AgentGUI PRIVATE IMGUI_USE_WCHAR32)

if (TARGET OpenGL::GL)
    set(OPENGL_TARGET OpenGL::GL)
elseif (TARGET OpenGL::OpenGL)
    set(OPENGL_TARGET OpenGL::OpenGL)
else()
    set(OPENGL_TARGET ${OPENGL_LIBRARIES})
endif()

target_link_libraries(AgentGUI
    ${SDL2_LIBRARIES}
    ${OPENGL_TARGET}
    dl
)

message(STATUS "Agent GUI (root layout) configured.")

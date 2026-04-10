cmake_minimum_required(VERSION 3.15)
project(EMPI CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EMPI_BUILD_LLAMA_TOOLS "Build llama-based dialog recorder" ON)

if(EMPI_BUILD_LLAMA_TOOLS)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE) 
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    
    add_subdirectory(llama-dynamic-context)
    
    message(STATUS "Llama integration enabled")
else()
    message(STATUS "Llama integration disabled")
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    message(STATUS "Python found: ${Python3_EXECUTABLE}")
else()
    message(WARNING "Python not found - TextAnalyzer will not work at runtime")
endif()

# ============================================================================
# MAIN AGENTS LIBRARY
# ============================================================================
add_library(empi_agents STATIC
    src/agents/TextAnalyzer.cpp
    src/agents/FeedbackAgent.cpp
    src/agents/InterfaceGenerator.cpp
    src/core/UniversalAgent.cpp
)

target_include_directories(empi_agents
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/agents
        ${CMAKE_CURRENT_BINARY_DIR}
)

# ============================================================================
# JSON DEPENDENCY
# ============================================================================
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(empi_agents PRIVATE nlohmann_json::nlohmann_json)
    message(STATUS "Using system nlohmann/json")
else()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
    target_link_libraries(empi_agents PRIVATE nlohmann_json::nlohmann_json)
    message(STATUS "Using downloaded nlohmann/json")
endif()

# ============================================================================
# LLAMA.CPP INTEGRATION
# ============================================================================
if(EMPI_BUILD_LLAMA_TOOLS)
    target_include_directories(empi_agents PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/llama-dynamic-context/include
    )

    target_link_libraries(empi_agents PUBLIC
        llama
        ggml
        pthread
        dl
    )
endif()

# ============================================================================
# FILESYSTEM SUPPORT (older GCC)
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(empi_agents PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(empi_agents PRIVATE c++fs)
endif()

# ============================================================================
# COPY PYTHON INTEGRATIONS
# ============================================================================
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/integrations)
add_custom_command(
    TARGET empi_agents POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/integrations/
        ${CMAKE_CURRENT_BINARY_DIR}/integrations/
    COMMENT "Copying Python integrations to binary directory"
)

# ============================================================================
# EXECUTABLES
# ============================================================================

# 1. Dialog recorder tool
if(EMPI_BUILD_LLAMA_TOOLS AND TARGET llama)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/dialog_recorder.cpp)
        add_executable(empi_dialog_recorder
            tools/dialog_recorder.cpp
        )
        target_link_libraries(empi_dialog_recorder PRIVATE llama)
        target_include_directories(empi_dialog_recorder PRIVATE
            llama-dynamic-context/include
        )
        message(STATUS "Building empi_dialog_recorder")
    else()
        message(STATUS "tools/dialog_recorder.cpp not found - skipping")
    endif()
endif()

# 2. Test executables
add_executable(test_default 
    tests/test_default.cpp
)
target_link_libraries(test_default PRIVATE empi_agents)

add_executable(test_text_analyzer 
    tests/test_text_analyzer.cpp
)
target_link_libraries(test_text_analyzer PRIVATE empi_agents)

add_executable(test_orchestration 
    tests/test_orchestration.cpp
)
target_link_libraries(test_orchestration PRIVATE empi_agents)

# 3. MAIN EXPERIMENT SCRIPT (NEW)
add_executable(run_full_experiment 
    tests/run_full_experiment.cpp
)
target_link_libraries(run_full_experiment PRIVATE empi_agents)

# ============================================================================
# TESTING (optional)
# ============================================================================
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_test(NAME TextAnalyzerTest COMMAND test_text_analyzer)
endif()

# ============================================================================
# INSTALL
# ============================================================================
install(TARGETS empi_agents
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY src/
    DESTINATION include/empi
    FILES_MATCHING PATTERN "*.hpp"
)

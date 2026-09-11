cmake_minimum_required(VERSION 3.24)
project(ThanLongTestSell VERSION 1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ThanLongTestSell is Windows-only")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ThanLongTestSell must be built x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Transport-only split. CMake joins each source byte-for-byte before compiling.
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(GENERATED_BRIDGE "${GENERATED_DIR}/bridge.cpp")
file(WRITE "${GENERATED_BRIDGE}" "")
foreach(PART
    src/bridge_chunks/bridge_01.txt
    src/bridge_chunks/bridge_02.txt
    src/bridge_chunks/bridge_03.txt
    src/bridge_chunks/bridge_04.txt
    src/bridge_chunks/bridge_05.txt
    src/bridge_chunks/bridge_06.txt
    src/bridge_chunks/bridge_07.txt
    src/bridge_chunks/bridge_08.txt
    src/bridge_chunks/bridge_09.txt
    src/bridge_chunks/bridge_10.txt
    src/bridge_chunks/bridge_11.txt
    src/bridge_chunks/bridge_12.txt
    src/bridge_chunks/bridge_13.txt)
    file(READ "${CMAKE_SOURCE_DIR}/${PART}" PART_TEXT)
    file(APPEND "${GENERATED_BRIDGE}" "${PART_TEXT}")
endforeach()

set(GENERATED_CONTROLLER "${GENERATED_DIR}/controller.cpp")
file(WRITE "${GENERATED_CONTROLLER}" "")
foreach(PART
    src/controller_chunks/controller_01.txt
    src/controller_chunks/controller_02.txt
    src/controller_chunks/controller_03.txt
    src/controller_chunks/controller_04.txt)
    file(READ "${CMAKE_SOURCE_DIR}/${PART}" PART_TEXT)
    file(APPEND "${GENERATED_CONTROLLER}" "${PART_TEXT}")
endforeach()

add_library(ThanLongTestSellBridge SHARED "${GENERATED_BRIDGE}")
target_include_directories(ThanLongTestSellBridge PRIVATE src)
target_compile_definitions(ThanLongTestSellBridge PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ThanLongTestSellBridge PRIVATE user32 kernel32)

add_executable(ThanLongTestSell WIN32 "${GENERATED_CONTROLLER}")
target_include_directories(ThanLongTestSell PRIVATE src)
target_compile_definitions(ThanLongTestSell PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(ThanLongTestSell PRIVATE comctl32 user32 kernel32 gdi32 windowscodecs ole32)

# Dedicated lightweight runtime proof for Trade lifecycle. It shares the same
# semantic bridge but does not scan bag items, OCR, or invoke trade actions.
add_executable(ThanLongTradeStateTest WIN32 src/trade_test.cpp)
target_include_directories(ThanLongTradeStateTest PRIVATE src)
target_compile_definitions(ThanLongTradeStateTest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(ThanLongTradeStateTest PRIVATE comctl32 user32 kernel32)

add_custom_command(TARGET ThanLongTestSell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/test_sell.ini"
        "$<TARGET_FILE_DIR:ThanLongTestSell>/test_sell.ini")

if(MSVC)
    target_compile_options(ThanLongTestSellBridge PRIVATE /utf-8 /W4 /permissive- /O2)
    target_compile_options(ThanLongTestSell PRIVATE /utf-8 /W4 /permissive- /O2)
    target_compile_options(ThanLongTradeStateTest PRIVATE /utf-8 /W4 /permissive- /O2)
    target_link_options(ThanLongTestSellBridge PRIVATE /OPT:REF /OPT:ICF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
    target_link_options(ThanLongTestSell PRIVATE /OPT:REF /OPT:ICF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
    target_link_options(ThanLongTradeStateTest PRIVATE /OPT:REF /OPT:ICF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
endif()

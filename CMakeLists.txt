cmake_minimum_required(VERSION 3.13)


cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
# include(pico_sdk_import.cmake)
include(FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/pico_sdk_import.cmake)

PROJECT(examples)

# project(ForwardToPicoProject C CXX ASM)
add_subdirectory(FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard)
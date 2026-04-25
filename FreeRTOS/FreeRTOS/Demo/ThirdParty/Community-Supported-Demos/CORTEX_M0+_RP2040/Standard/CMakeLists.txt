# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in FreeRTOS
include(FreeRTOS_Kernel_import.cmake)

if (NOT FREERTOS_DEMO_PATH)
        if (DEFINED ENV{FREERTOS_DEMO_PATH})
                set(FREERTOS_DEMO_PATH $ENV{FREERTOS_DEMO_PATH})
                message("Using FREERTOS_DEMO_PATH from environment ('${FREERTOS_DEMO_PATH}')")
        elseif(EXISTS ${FREERTOS_KERNEL_PATH}/../Demo)
                set(FREERTOS_DEMO_PATH ${FREERTOS_KERNEL_PATH}/../Demo)
        else ()
                message(FATAL_ERROR "FreeRTOS demo path was not specified. Please set FREERTOS_DEMO_PATH.")
        endif ()
endif ()

project(example C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

function(add_single_test_bin target_name)
        add_executable(${target_name}
                main.c
                ${target_name}.c
                )

        target_compile_definitions(${target_name} PRIVATE
                mainCREATE_SIMPLE_BLINKY_DEMO_ONLY=0
                ${ARGN}
                )

        target_include_directories(${target_name} PRIVATE
                ${CMAKE_CURRENT_LIST_DIR}
                ${FREERTOS_DEMO_PATH}/Common/include)

        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANG_AND_ID:C,GNU>:-Og>
                $<$<COMPILE_LANG_AND_ID:C,GNU>:-g3>
                $<$<COMPILE_LANG_AND_ID:C,GNU>:-fno-inline>
                )

        target_link_libraries(${target_name} pico_stdlib FreeRTOS-Kernel FreeRTOS-Kernel-Heap4)
        pico_add_extra_outputs(${target_name})
endfunction()

set(EDF_COMMON_DEFS
        configUSE_EDF_SCHEDULING=1
        configNUMBER_OF_CORES=1
        configTICK_CORE=0
        configRUN_MULTIPLE_PRIORITIES=0
        configUSE_CORE_AFFINITY=0
        configUSE_PASSIVE_IDLE_HOOK=0
        configUSE_SRP=1
        configUSE_CBS=0
        configGLOBAL_EDF_ENABLE=1
        configPARTITIONED_EDF_ENABLE=0
        configEDF_TRACE_ENABLE=1
        )

set(SRP_COMMON_DEFS
        configUSE_EDF_SCHEDULING=1
        configNUMBER_OF_CORES=1
        configTICK_CORE=0
        configRUN_MULTIPLE_PRIORITIES=0
        configUSE_CORE_AFFINITY=0
        configUSE_PASSIVE_IDLE_HOOK=0
        configUSE_SRP=1
        configUSE_CBS=0
        configGLOBAL_EDF_ENABLE=1
        configPARTITIONED_EDF_ENABLE=0
        configEDF_TRACE_ENABLE=1
        )

set(CBS_COMMON_DEFS
        configUSE_EDF_SCHEDULING=1
        configNUMBER_OF_CORES=1
        configTICK_CORE=0
        configRUN_MULTIPLE_PRIORITIES=0
        configUSE_CORE_AFFINITY=0
        configUSE_PASSIVE_IDLE_HOOK=0
        configUSE_SRP=0
        configUSE_CBS=1
        configGLOBAL_EDF_ENABLE=1
        configPARTITIONED_EDF_ENABLE=0
        configEDF_TRACE_ENABLE=1
        )

set(SMP_COMMON_DEFS
        configUSE_EDF_SCHEDULING=1
        configNUMBER_OF_CORES=2
        configTICK_CORE=0
        configRUN_MULTIPLE_PRIORITIES=1
        configUSE_CORE_AFFINITY=1
        configUSE_PASSIVE_IDLE_HOOK=0
        configUSE_SRP=0
        configUSE_CBS=0
        configEDF_TRACE_ENABLE=1
        )

set(SMP_GLOBAL_DEFS
        configGLOBAL_EDF_ENABLE=1
        configPARTITIONED_EDF_ENABLE=0
        )

set(SMP_PARTITION_DEFS
        configGLOBAL_EDF_ENABLE=0
        configPARTITIONED_EDF_ENABLE=1
        )

###############################################################################################
#                                       EDF
###############################################################################################
# EDF test target (from Assignment Guide Section 5)
add_single_test_bin(main_edf_test
        mainCREATE_EDF_TEST=1
        ${EDF_COMMON_DEFS})

# Previous EDF test variant target
add_single_test_bin(main_edf_test_previous
        mainCREATE_EDF_TEST=1
        ${EDF_COMMON_DEFS})

# EDF dynamic admission control test (8 tasks: 6 startup + 2 runtime + reject + accept)
add_single_test_bin(main_edf_test_dynamic
        mainCREATE_EDF_TEST=1
        ${EDF_COMMON_DEFS})

# EDF 100-task admission control test (professor README requirement)
add_single_test_bin(main_edf_test_100
        mainCREATE_EDF_TEST=1
        ${EDF_COMMON_DEFS})

###############################################################################################
#                                       SRP
###############################################################################################
# SRP dynamic test (3 multi-unit resources, nested access, runtime admit/reject)
add_single_test_bin(main_srp_test_dynamic
        mainCREATE_EDF_TEST=1
        ${SRP_COMMON_DEFS})

# SRP 100-task quantitative test (professor README requirement)
add_single_test_bin(main_srp_test_100
        mainCREATE_EDF_TEST=1
        ${SRP_COMMON_DEFS})

# SRP 100-task stack-sharing quantitative test (professor README requirement)
add_single_test_bin(main_srp_test_100_sharing
        mainCREATE_EDF_TEST=1
        configSRP_STACK_SHARING=1
        ${SRP_COMMON_DEFS})

###############################################################################################
#                                       CBS
###############################################################################################
# CBS basic (high utilization)
add_single_test_bin(main_cbs_test
        mainCREATE_CBS_TEST=1
        ${CBS_COMMON_DEFS})

# CBS workload test. Multiple tasks, dynamic task dispatch
add_single_test_bin(main_cbs_test_dynamic_multi
        mainCREATE_CBS_TEST=1
        ${CBS_COMMON_DEFS})

# CBS workload test. Impossible execution workload. Test overrun management.
add_single_test_bin(main_cbs_test_overrun
        mainCREATE_CBS_TEST=1
        ${CBS_COMMON_DEFS})


###############################################################################################
#                                       SMP
###############################################################################################
add_single_test_bin(main_smp_global_test_admission
        mainCREATE_EDF_TEST=1
        ${SMP_COMMON_DEFS}
        ${SMP_GLOBAL_DEFS})

add_single_test_bin(main_smp_global_test_migration
        mainCREATE_EDF_TEST=1
        ${SMP_COMMON_DEFS}
        ${SMP_GLOBAL_DEFS})

add_single_test_bin(main_smp_partition_test_fit
        mainCREATE_EDF_TEST=1
        ${SMP_COMMON_DEFS}
        ${SMP_PARTITION_DEFS})

add_single_test_bin(main_smp_partition_test_migration
        mainCREATE_EDF_TEST=1
        ${SMP_COMMON_DEFS}
        ${SMP_PARTITION_DEFS})
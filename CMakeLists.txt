# Copyright (C) 2023, Proyectos y Sistemas de Mantenimiento SL (eProsima)
#
# This program is commercial software licensed under the terms of the
# eProsima Software License Agreement Rev 03 (the "License")
#
# You may obtain a copy of the License at
# https://www.eprosima.com/licenses/LICENSE-REV03

cmake_minimum_required(VERSION 3.5)

project(ros2_services)

if(NOT safedds_FOUND)
    find_package(safedds REQUIRED)
endif()

find_program(SAFEDDSGEN safeddsgen PATHS /home/administrator/Safe-DDS-source-release/code-gen/scripts)

if(SAFEDDSGEN STREQUAL "SAFEDDSGEN-NOTFOUND")
    message(STATUS "safeddsgen not found. Skipping ${PROJECT_NAME} example.")
else()
    # IDL Files
    set(
        IDL_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/idl/GetControlValueReq.idl
        ${CMAKE_CURRENT_SOURCE_DIR}/idl/GetControlValueRes.idl
    )

    # Generated type support files
    foreach(IDL_FILE ${IDL_FILES})
        # Replace .idl extension with .hpp and add to GENERATED_TYPE_SUPPORT_FILE
        string(REPLACE ".idl" ".hpp" GENERATED_TYPE_SUPPORT_FILE ${IDL_FILE})
        list(APPEND GENERATED_TYPE_SUPPORT_FILES ${GENERATED_TYPE_SUPPORT_FILE})
    endforeach()

    # Create folder
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/idl)

    # Build type support files
    add_custom_command(
        BYPRODUCTS ${GENERATED_TYPE_SUPPORT_FILES}
        OUTPUT ${GENERATED_TYPE_SUPPORT_FILES}
        COMMAND ${SAFEDDSGEN}
        -D ${CMAKE_CURRENT_BINARY_DIR}/idl/
        ${IDL_FILES}
        DEPENDS ${IDL_FILES}
        COMMENT "Safe DDS type support generation" VERBATIM
    )

    # Service server
    add_executable(ros2_service_server main_server.cpp ${GENERATED_TYPE_SUPPORT_FILES})
    target_link_libraries(ros2_service_server safedds)
    target_include_directories(ros2_service_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/)

    # Service client
    add_executable(ros2_service_client main_client.cpp ${GENERATED_TYPE_SUPPORT_FILES})
    target_link_libraries(ros2_service_client safedds)
    target_include_directories(ros2_service_client PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/)
endif()
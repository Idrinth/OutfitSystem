cmake_minimum_required(VERSION 3.25)

project(
        IdrinthOutfitSystem
        VERSION 1.0.0
        HOMEPAGE_URL https://github.com/Idrinth/IdrinthOutfitSystem
        LANGUAGES CXX)
set(PROJECT_LICENSE "MIT")
set(PROJECT_FRIENDLY_NAME ${PROJECT_NAME})
set(PROJECT_LONG_NAME "A simple Outfit-Like System")
set(PROJECT_DESCRIPTION ${PROJECT_LONG_NAME})
set(PROJECT_COPYRIGHT "MIT")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(addpluginsources)
include(pluginconfig)
include(plugintarget)
include(pluginpackage)
include("${PROJECT_SOURCE_DIR}/local.cmake" OPTIONAL)

######## dependencies
set(Boost_NO_WARN_NEW_VERSIONS 1)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED  ON)
find_package(Boost CONFIG REQUIRED COMPONENTS)
if(Boost_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${Boost_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
endif()

find_package(ryml CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ryml::ryml)

########################################################################################################################
## Automatic plugin packaging
########################################################################################################################
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Scripts/Source/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/Source/Scripts/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/bin/Release/${PROJECT_NAME}.pdb" "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/dist/${PROJECT_NAME}.yaml" "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E tar cfv "${PROJECT_NAME}.zip" --format=zip "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/${PROJECT_NAME}.yaml" "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E tar cfv "${PROJECT_NAME}-pdb.zip" --format=zip "${CMAKE_CURRENT_BINARY_DIR}/SKSE/Plugins/${PROJECT_NAME}.pdb")
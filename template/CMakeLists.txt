cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES CXX)

include(compilerconfig)
include(defaults)
include(helpers)

set(SOCIAL_COMMENTS_GOOGLE_CLIENT_ID "" CACHE STRING "Preconfigured Google OAuth desktop Client ID")
set(SOCIAL_COMMENTS_FACEBOOK_BROKER_URL "" CACHE STRING "Preconfigured HTTPS OAuth broker base URL")

if(DEFINED ENV{SOCIAL_COMMENTS_GOOGLE_CLIENT_ID} AND SOCIAL_COMMENTS_GOOGLE_CLIENT_ID STREQUAL "")
    set(SOCIAL_COMMENTS_GOOGLE_CLIENT_ID "$ENV{SOCIAL_COMMENTS_GOOGLE_CLIENT_ID}")
endif()
if(DEFINED ENV{SOCIAL_COMMENTS_FACEBOOK_BROKER_URL} AND SOCIAL_COMMENTS_FACEBOOK_BROKER_URL STREQUAL "")
    set(SOCIAL_COMMENTS_FACEBOOK_BROKER_URL "$ENV{SOCIAL_COMMENTS_FACEBOOK_BROKER_URL}")
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Advapi32)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/plugin-main.cpp
    src/social-comments-dock.cpp
    src/social-comments-dock.hpp
    src/social-api.cpp
    src/social-api.hpp
    src/oauth-manager.cpp
    src/oauth-manager.hpp
    src/overlay-server.cpp
    src/overlay-server.hpp
    src/comment-model.hpp
    src/secret-store.cpp
    src/secret-store.hpp
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    QT_NO_KEYWORDS
    SOCIAL_COMMENTS_GOOGLE_CLIENT_ID="${SOCIAL_COMMENTS_GOOGLE_CLIENT_ID}"
    SOCIAL_COMMENTS_FACEBOOK_BROKER_URL="${SOCIAL_COMMENTS_FACEBOOK_BROKER_URL}"
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

if(MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4 /permissive-)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

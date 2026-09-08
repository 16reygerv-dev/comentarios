cmake_minimum_required(VERSION 3.28)
project(obs-social-comments VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(SOCIAL_COMMENTS_GOOGLE_CLIENT_ID "" CACHE STRING "Preconfigured Google OAuth desktop Client ID")
set(SOCIAL_COMMENTS_FACEBOOK_BROKER_URL "" CACHE STRING "Preconfigured HTTPS OAuth broker base URL")

if(DEFINED ENV{SOCIAL_COMMENTS_GOOGLE_CLIENT_ID} AND SOCIAL_COMMENTS_GOOGLE_CLIENT_ID STREQUAL "")
    set(SOCIAL_COMMENTS_GOOGLE_CLIENT_ID "$ENV{SOCIAL_COMMENTS_GOOGLE_CLIENT_ID}")
endif()
if(DEFINED ENV{SOCIAL_COMMENTS_FACEBOOK_BROKER_URL} AND SOCIAL_COMMENTS_FACEBOOK_BROKER_URL STREQUAL "")
    set(SOCIAL_COMMENTS_FACEBOOK_BROKER_URL "$ENV{SOCIAL_COMMENTS_FACEBOOK_BROKER_URL}")
endif()

add_library(obs-social-comments MODULE
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

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

target_link_libraries(obs-social-comments PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    target_link_libraries(obs-social-comments PRIVATE Advapi32)
endif()

target_compile_definitions(obs-social-comments PRIVATE
    QT_NO_KEYWORDS
    SOCIAL_COMMENTS_GOOGLE_CLIENT_ID="${SOCIAL_COMMENTS_GOOGLE_CLIENT_ID}"
    SOCIAL_COMMENTS_FACEBOOK_BROKER_URL="${SOCIAL_COMMENTS_FACEBOOK_BROKER_URL}"
)
set_target_properties(obs-social-comments PROPERTIES PREFIX "" OUTPUT_NAME "obs-social-comments")

if(MSVC)
    target_compile_options(obs-social-comments PRIVATE /W4 /permissive-)
endif()

install(TARGETS obs-social-comments
    RUNTIME DESTINATION obs-plugins/64bit
    LIBRARY DESTINATION obs-plugins/64bit
)

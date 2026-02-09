cmake_minimum_required(VERSION 3.28)
project(uSEEDLinkToDataPacketImportProxy VERSION 0.0.1 LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

configure_file(${CMAKE_SOURCE_DIR}/src/version.hpp.in
               ${CMAKE_SOURCE_DIR}/src/version.hpp)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
if (NOT ${BUILD_SHARED_LIBS})
   set(Boost_USE_STATIC_LIBS ON) 
endif()
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
find_package(TBB REQUIRED)
find_package(spdlog REQUIRED)
find_package(libmseed REQUIRED)
find_package(SEEDLink REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(utf8_range CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(opentelemetry-cpp REQUIRED)
find_package(TBB COMPONENTS tbb REQUIRED)
find_package(Boost COMPONENTS program_options REQUIRED)
find_package(Threads REQUIRED)
find_package(Catch2)

set(PROTO_SRC
    uDataPacketImportAPI/v1/data_type.proto
    uDataPacketImportAPI/v1/stream_identifier.proto
    uDataPacketImportAPI/v1/packet.proto
    uDataPacketImportAPI/v1/publish_response.proto
    uDataPacketImportAPI/v1/frontend.proto)

set(LIBRARY_SRC
    src/grpcOptions.cpp
    src/seedLinkClient.cpp
    src/seedLinkClientOptions.cpp
    src/streamSelector.cpp
    src/seedLinkClient.cpp
    src/version.cpp
    ${PROTO_SRC})
set(HEADER_FILES
    src/grpcOptions.hpp
    src/seedLinkClient.hpp
    src/seedLinkClientOptions.hpp
    src/streamSelector.hpp
    src/seedLinkClient.hpp
    src/version.hpp)
set(MODULE_FILES
    src/modules/logger.cppm
    src/modules/metrics.cppm
    src/modules/packetConverter.cppm
    src/modules/packetWriter.cppm
    src/modules/programOptions.cppm
    src/modules/otelSpdlogSink.cppm)

if (${BUILD_SHARED_LIBS})
   add_library(libuSEEDLinkToDataPacketImportProxy STATIC ${LIBRARY_SRC})
else()
   add_library(libuSEEDLinkToDataPacketImportProxy STATIC ${LIBRARY_SRC})
endif()
add_library(uSEEDLinkToDataPacketImportProxy::libuSEEDLinkToDataPacketImportProxy ALIAS libuSEEDLinkToDataPacketImportProxy)
set_target_properties(libuSEEDLinkToDataPacketImportProxy PROPERTIES
                      OUTPUT_NAME uSEEDLinkToDataPacketImportProxy
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_include_directories(libuSEEDLinkToDataPacketImportProxy
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/modules>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                          )
target_link_libraries(libuSEEDLinkToDataPacketImportProxy
                      PUBLIC TBB::tbb
                      PRIVATE gRPC::grpc
                              gRPC::grpc++
                              protobuf::libprotobuf
                              opentelemetry-cpp::otlp_http_log_record_exporter
                              opentelemetry-cpp::otlp_http_metric_exporter
                              spdlog::spdlog_header_only
                              Boost::headers
                              Boost::program_options
                              Threads::Threads)
target_sources(libuSEEDLinkToDataPacketImportProxy
               PUBLIC FILE_SET
                      HEADERS
                      BASE_DIRS
                         src
                      FILES
                         ${HEADER_FILES}
               PUBLIC FILE_SET
                      CXX_MODULES
                      BASE_DIRS
                         src/modules
                      FILES
                         ${MODULE_FILES}
               )
protobuf_generate(TARGET libuSEEDLinkToDataPacketImportProxy
                  LANGUAGE cpp
                 )
protobuf_generate(TARGET libuSEEDLinkToDataPacketImportProxy
                  LANGUAGE grpc
                  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                  PLUGIN_OPTIONS generate_mock_code=true
                  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                 )

add_executable(uSEEDLinkToDataPacketImportProxy
               src/main.cpp)
#               src/seedLinkClient.cpp
#               ${COMMON_SRC})
set_target_properties(uSEEDLinkToDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_include_directories(uSEEDLinkToDataPacketImportProxy
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                          )
target_link_libraries(uSEEDLinkToDataPacketImportProxy
                      PUBLIC
                         TBB::tbb
                      PRIVATE
                         uSEEDLinkToDataPacketImportProxy::libuSEEDLinkToDataPacketImportProxy
                         gRPC::grpc
                         gRPC::grpc++
                         opentelemetry-cpp::otlp_http_log_record_exporter
                         opentelemetry-cpp::otlp_http_metric_exporter
                         spdlog::spdlog_header_only
                         SEEDLink::SEEDLink
                         mseed::mseed
                         Boost::headers
                         Boost::program_options
                         Threads::Threads)

##########################################################################################
#                                         Tests                                          #
##########################################################################################

add_executable(unitTests
               testing/packetConverter.cpp
               testing/seedLink.cpp
               testing/grpc.cpp
               )
set_target_properties(unitTests PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 
target_link_libraries(unitTests
                      PUBLIC
                         TBB::tbb
                      PRIVATE  
                         uSEEDLinkToDataPacketImportProxy::libuSEEDLinkToDataPacketImportProxy
                         gRPC::grpc
                         gRPC::grpc++
                         #gRPC::grpcpp_otel_plugin
                         SEEDLink::SEEDLink
                         mseed::mseed
                         Threads::Threads
                         Catch2::Catch2 Catch2::Catch2WithMain)
target_include_directories(unitTests
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
add_test(NAME unitTests 
         COMMAND unitTests)

##########################################################################################
#                                      Installation                                      #
##########################################################################################
include(CMakePackageConfigHelpers)
configure_file(${PROJECT_SOURCE_DIR}/cmake/uSEEDLinkToDataPacketImportProxy.cmake.in
               uSEEDLinkToDataPacketImportProxyConfig.cmake @ONLY)
write_basic_package_version_file(
   uSEEDLinkToDataPacketImportProxyConfigVersion.cmake
   VERSION ${PACKAGE_VERSION}
   COMPATIBILITY AnyNewerVersion)

include(GNUInstallDirs)
install(TARGETS libuSEEDLinkToDataPacketImportProxy
        EXPORT ${PROJECT_NAME}Targets
        FILE_SET HEADERS 
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILE_SET CXX_MODULES
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT libraries)
install(TARGETS uSEEDLinkToDataPacketImportProxy
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT applications)
export(EXPORT ${PROJECT_NAME}Targets
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/uSEEDLinkToDataPacketImportProxyConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/uSEEDLinkToDataPacketImportProxyConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uSEEDLinkToDataPacketImportProxy)

##########################################################################################
#                                        Containers                                      #
##########################################################################################
include(CPackComponent)
if (${BUILD_FOR_DEBIAN})
   message("Will configure debian container")
   configure_file(${CMAKE_CURRENT_SOURCE_DIR}/scripts/Dockerfile.deb.in
                  ${CMAKE_CURRENT_BINARY_DIR}/Dockerfile.deb @ONLY)
   add_custom_target(${PROJECT_NAME}-deb
                     COMMENT "Creating deb file for ${PROJECT_NAME}"
                     COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
   add_dependencies(${PROJECT_NAME}-deb uSEEDLinkToDataPacketImportProxy libuSEEDLinkToDataPacketImportProxy)
   find_program(buildah_EXECUTABLE buildah)
   if (buildah_EXECUTABLE)
      add_custom_target(buildah
                        COMMENT "Preparing the container with buildah"
                        COMMAND ${buildah_EXECUTABLE} bud -t useedlinktodatapacketimportproxy:${PROJECT_VERSION} -t useedlinktodatapacketimportproxy:latest -f ./Dockerfile.deb
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
      add_dependencies(buildah ${PROJECT_NAME}-deb)
   endif()
endif()


##########################################################################################
#                                     CPACK Packaging                                    #
##########################################################################################
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "UUSS")
set(CPACK_PACKAGE_CONTACT "ben.baker@utah.edu")
set(CPACK_PACKAGE_LICENSE "MIT")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Propagates packets from SEEDLink to the data packet import proxy in the UUSS Kubernetes environment.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
cpack_add_component(libraries
                    DISPLAY_NAME "uSEEDLinkToDataPacketImportProxy common libraries")
cpack_add_component(headers
                    DISPLAY_NAME "uSEEDLinkToDataPacketImportProxy library header files"
                    DEPENDS libraries)
cpack_add_component(applications
                    DISPLAY_NAME "uSEEDLinkToDataPacketImportProxy applications"
                    DEPENDS libraries)
set(CPACK_GENERATOR ZIP TGZ)
if (WIN32)
   list(APPEND CPACK_GENERATOR WIX)
elseif (APPLE)
   list(APPEND CPACK_GENERATOR productbuild)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
   find_program(RPMBUILD_PROGRAM rpmbuild)
   if (RPMBUILD_PROGRAM)
      list(APPEND CPACK_GENERATOR RPM)
   endif()
   #if (${BUILD_FOR_DEBIAN})
   #   find_program(DPKG_PROGRAM dpkg REQUIRED)
   #else()
   #   find_program(DPKG_PROGRAM dpkg)
   #endif()
   #if (DPKG_PROGRAM)
   #   list(APPEND CPACK_GENERATOR DEB)
   #   set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
   #   set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) 
   #endif()
else()
   message("Unknown operating system")
endif()
set(CPACK_SOURCE_IGNORE_FILES
  /\\.git/
  \\.swp
  \\.orig
  /CMakeLists\\.txt\\.user
  /private/
)
include(CPack) # Put this last!

cmake_minimum_required(VERSION 3.28)
project(uSEEDLinkToDataPacketImportProxy VERSION 0.0.1 LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)
option(BUILD_WITH_OTEL_LOGGING "Build with OpenTelemetry logging" ON)

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

set(COMMON_SRC
    src/grpcOptions.cpp
    src/streamSelector.cpp
    src/seedLinkClientOptions.cpp
    src/otelSpdlogSink.cpp
    src/version.cpp
    ${PROTO_SRC})
add_executable(uSEEDLinkToDataPacketImportProxy
               src/main.cpp
               src/seedLinkClient.cpp
               ${COMMON_SRC})
set_target_properties(uSEEDLinkToDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
if (${BUILD_WITH_OTEL_LOGGING})
   add_compile_definitions(uSEEDLinkToDataPacketImportProxy PRIVATE USE_OTEL_LOGGING)
endif()
target_include_directories(uSEEDLinkToDataPacketImportProxy
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                          )
target_link_libraries(uSEEDLinkToDataPacketImportProxy
                      PRIVATE gRPC::grpc
                              gRPC::grpc++
                              protobuf::libprotobuf
                              opentelemetry-cpp::prometheus_exporter
                              #opentelemetry-cpp::otlp_http_log_record_exporter
                              #opentelemetry-cpp::ostream_log_record_exporter
                              spdlog::spdlog_header_only
                              SEEDLink::SEEDLink
                              mseed::mseed
                              TBB::tbb
                              Boost::headers
                              Boost::program_options
                              Threads::Threads)
protobuf_generate(TARGET uSEEDLinkToDataPacketImportProxy
                  LANGUAGE cpp 
                 )
protobuf_generate(TARGET uSEEDLinkToDataPacketImportProxy
                  LANGUAGE grpc
                  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                  PLUGIN_OPTIONS generate_mock_code=true
                  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                 )

##########################################################################################
#                                         Tests                                          #
##########################################################################################

add_executable(unitTests
               testing/packetConverter.cpp
               testing/seedLink.cpp
               ${COMMON_SRC})
set_target_properties(unitTests PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES 
                      CXX_EXTENSIONS NO) 
target_link_libraries(unitTests
                      gRPC::grpc
                      gRPC::grpc++
                      gRPC::grpcpp_otel_plugin
                      SEEDLink::SEEDLink
                      mseed::mseed
                      Threads::Threads
                      Catch2::Catch2 Catch2::Catch2WithMain)
target_include_directories(unitTests
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/data>
                           #PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
add_test(NAME unitTests 
         COMMAND unitTests)
protobuf_generate(TARGET unitTests
                  LANGUAGE cpp
                 )
protobuf_generate(TARGET unitTests
                  LANGUAGE grpc
                  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
                  PLUGIN_OPTIONS generate_mock_code=true
                  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
                 )


##########################################################################################
#                                      Installation                                      #
##########################################################################################
include(GNUInstallDirs)
#include(CMakePackageConfigHelpers)
#configure_package_config_file(
#    cmake/${PROJECT_NAME}Config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
#    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
#write_basic_package_version_file(
#    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
#    VERSION "${version}"
#    COMPATIBILITY AnyNewerVersion
#)

#install(TARGETS uDataPacketImport
#        EXPORT ${PROJECT_NAME}-targets
#        FILE_SET HEADERS
#        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#        COMPONENT libraries)
install(TARGETS uSEEDLinkToDataPacketImportProxy
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT applications)
#export(EXPORT ${PROJECT_NAME}-targets
#       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
#install(EXPORT ${PROJECT_NAME}-targets
#        NAMESPACE ${PROJECT_NAME}::
#        FILE ${PROJECT_NAME}Targets.cmake
#        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
#install(FILES
#        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
#        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
#        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

##########################################################################################
#                                     CPACK Packaging                                    #
##########################################################################################
include(CPackComponent)
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
#if (${BUILD_SHARED_LIBS})
#   cpack_add_component(libraries
#                       DISPLAY_NAME "uDataPacketImport common libraries"
#                       DEPENDS ${GRPC_LIBRARY} ${GRPC_GRPC++_LIBRARY} ${SEEDLink_LIBRARIES})
#else()
#   cpack_add_component(libraries
#                       DISPLAY_NAME "uDataPacketImport common libraries")
#endif()
#cpack_add_component(headers
#                    DISPLAY_NAME "uDataPacketImport library header files"
#                    DEPENDS libraries)
cpack_add_component(applications
                    DISPLAY_NAME "uSEEDLinkToDataPacketImportProxy applications")
#                    DEPENDS libraries)
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
   if (${BUILD_FOR_DEBIAN})
      find_program(DPKG_PROGRAM dpkg REQUIRED)
   else()
      find_program(DPKG_PROGRAM dpkg)
   endif()
   if (DPKG_PROGRAM)
      list(APPEND CPACK_GENERATOR DEB)
      set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}")
      set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) 
   endif()
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

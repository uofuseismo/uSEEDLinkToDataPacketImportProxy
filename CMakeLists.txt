cmake_minimum_required(VERSION 3.28)
project(uSEEDLinkToDataPacketImportProxy VERSION 0.0.1 LANGUAGES CXX)
enable_testing()

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

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

add_executable(uSEEDLinkToDataPacketImportProxy
               src/main.cpp
               src/grpcOptions.cpp
               ${PROTO_SRC})
set_target_properties(uSEEDLinkToDataPacketImportProxy PROPERTIES
                      CXX_STANDARD 20
                      CXX_STANDARD_REQUIRED YES
                      CXX_EXTENSIONS NO)
target_include_directories(uSEEDLinkToDataPacketImportProxy
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                           PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
                          )
target_link_libraries(uSEEDLinkToDataPacketImportProxy
                      PRIVATE gRPC::grpc gRPC::grpc++ protobuf::libprotobuf
                              opentelemetry-cpp::prometheus_exporter
                              spdlog::spdlog_header_only
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


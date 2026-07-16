from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import cmake_layout, CMake, CMakeDeps, CMakeToolchain

class uSEEDLinkToDataPacketImportProxy(ConanFile):
   name = "uSEEDLinkToDataPacketImportProxy"
   #version = "0.0.1"
   license = "MIT"
   description = " Forwards packets from a SEEDLink import to the UUSS K8s import proxy service."
   url = "https://github.com/uofuseismo/uSEEDLinkToDataPacketImportProxy"
   settings = "os", "compiler", "build_type", "arch"
   options = {"build_tests" : [True, False],
              "build_training" : [True, False],
              "with_conan" : [True, False]}
   default_options = {"hwloc/*:shared": "True",
                      "opentelemetry-cpp/*:with_otlp_http": "True",
                      "opentelemetry-cpp/*:with_otlp_grpc": "True",
                      "opentelemetry-cpp/*:with_abi_v2" : "True",
                      "spdlog/*:header_only" : "True",
                      "build_tests" : "True",
                      "build_training" : "False",
                      "with_conan" : "True",}
   export_sources = "CMakeLists.txt", "LICENSE", "README.md", "cmake/*", "src/*", "testing/*"
   generators = "CMakeDeps", "CMakeToolchain"

   def requirements(self):
       # dependencies
       self.requires("grpc/1.82.0")
       self.requires("opentelemetry-cpp/1.26.0")
       self.requires("protobuf/6.33.5")
       self.requires("spdlog/1.17.0")
       self.requires("onetbb/2023.0.0")
       self.requires("boost/1.91.0")

   def build_requirements(self):
       # test dependncies and build tools
       self.test_requires("catch2/3.15.2")

   def layout(self):
       # defines the project layout
       cmake_layout(self)

   def build(self):
       # invokes the build system
       cmake = CMake(self)
       cmake.configure()
       cmake.build()

   def test(self):
       if can_run(self):
          cmake.test()

   def package(self):
       # copies files from the build to package folder
       cmake = CMake(self)
       cmake.install()

   def package_info(self):
       self.cpp_info.libs = ["uSEEDLinkToDataPacketImportProxy"]



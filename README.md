# About

This utility forwards packets from a SEEDLink server to the UUSS K8s import proxy service [uDataPacketImportProxy](https://github.com/uofuseismo/uDataPacketImportProxy).

# Proto Files

To obtain the proto files prior to compiling this software do the following:

    git subtree add --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash 

To update the proto files use 

    git subtree pull --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash

# Conan

    conan install . --build=missing -s build_type=Release -pr:a=Linux-x86_64-clang-21 --output-folder ./conanBuild
    cmake --preset conan-release -DWITH_CONAN=ON -DBUILD_CONTAINER=ON
    cmake --build --preset conan-release
    ctest --preset conan-release
    cmake --install conanBuild/build/Release


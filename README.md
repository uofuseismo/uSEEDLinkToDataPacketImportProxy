# About

This utility forwards packets from SEEDLink to the UUSS K8s import proxy service [uDataImportProxy](https://github.com/uofuseismo/uDataPacketImportProxy).

# Proto Files
To obtain the proto files prior to compiling this software do the following:

    git subtree add --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash 

To update the proto files use 

    git subtree pull --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash


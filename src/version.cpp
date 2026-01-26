#include <string>
#include "version.hpp"

using namespace USEEDLinkToDataPacketImportProxy;

int Version::getMajor() noexcept
{
    return uSEEDLinkToDataPacketImportProxy_MAJOR;
}

int Version::getMinor() noexcept
{
    return uSEEDLinkToDataPacketImportProxy_MINOR;
}

int Version::getPatch() noexcept
{
    return uSEEDLinkToDataPacketImportProxy_PATCH;
}

bool Version::isAtLeast(const int major, const int minor,
                        const int patch) noexcept
{
    if (uSEEDLinkToDataPacketImportProxy_MAJOR < major){return false;}
    if (uSEEDLinkToDataPacketImportProxy_MAJOR > major){return true;}
    if (uSEEDLinkToDataPacketImportProxy_MINOR < minor){return false;}
    if (uSEEDLinkToDataPacketImportProxy_MINOR > minor){return true;}
    if (uSEEDLinkToDataPacketImportProxy_PATCH < patch){return false;}
    return true;
}

std::string Version::getVersion() noexcept
{
    std::string version{uSEEDLinkToDataPacketImportProxy_VERSION};
    return version;
}


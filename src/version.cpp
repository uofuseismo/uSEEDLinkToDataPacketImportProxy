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

//NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool Version::isAtLeast(const int major, const int minor,
                        const int patch) noexcept
//NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (uSEEDLinkToDataPacketImportProxy_MAJOR < major){return false;}
    if (uSEEDLinkToDataPacketImportProxy_MAJOR > major){return true;}
    if (uSEEDLinkToDataPacketImportProxy_MINOR < minor){return false;}
    if (uSEEDLinkToDataPacketImportProxy_MINOR > minor){return true;}
    if (uSEEDLinkToDataPacketImportProxy_PATCH < patch){return false;}
    return true;
}

std::string Version::getTag() noexcept
{
    std::string tag{uSEEDLinkToDataPacketImportProxy_GITTAG};
    return tag;
}

std::string Version::getVersionWithTag() noexcept
{
    auto tag = Version::getTag();
    if (tag.empty())
    {
        return Version::getVersion();
    }
    else
    {
        return Version::getVersion() + "-" + tag;
    }
}

std::string Version::getVersion() noexcept
{
    std::string version{uSEEDLinkToDataPacketImportProxy_VERSION};
    return version;
}



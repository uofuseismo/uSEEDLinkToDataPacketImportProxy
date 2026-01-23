#include <string>
#include <algorithm>
#include <filesystem>
#include "grpcOptions.hpp"

using namespace USEEDLinkToDataPacketImportProxy;

class GRPCOptions::GRPCOptionsImpl
{
public:
    std::string mHost{"localhost"};
    std::string mAccessToken;
    std::string mServerCertificate;
    std::string mClientKey;
    std::string mClientCertificate;
    uint16_t mPort{50000};
    bool mHaveServerCertificate{false}; 
    bool mHaveClientKey{false};
    bool mHaveClientCertificate{false};
    bool mHaveAccessToken{false};
};

/// Constructor
GRPCOptions::GRPCOptions() :
    pImpl(std::make_unique<GRPCOptionsImpl> ())
{
}

/// Copy constructor
GRPCOptions::GRPCOptions(const GRPCOptions &options)
{
    *this = options;
}

/// Move constructor
GRPCOptions::GRPCOptions(GRPCOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
GRPCOptions& GRPCOptions::operator=(const GRPCOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<GRPCOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
GRPCOptions& GRPCOptions::operator=(GRPCOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
GRPCOptions::~GRPCOptions() = default; 

/// Host
void GRPCOptions::setHost(const std::string &hostIn)
{
    auto host = hostIn;
    host.erase(
       std::remove_if(host.begin(), host.end(), ::isspace),
       host.end());
    if (host.empty())
    {
        throw std::invalid_argument("Host is empty");
    }
    pImpl->mHost = host;
}

std::string GRPCOptions::getHost() const noexcept
{
    return pImpl->mHost;
}

std::string USEEDLinkToDataPacketImportProxy::makeAddress(
    const GRPCOptions &options)
{
    return options.getHost() + ":" + std::to_string(options.getPort());
}

/// Port
void GRPCOptions::setPort(const uint16_t port)
{
    if (port < 1)
    {
        throw std::invalid_argument("port must be positive");
    }
    pImpl->mPort = port;
}

uint16_t GRPCOptions::getPort() const noexcept
{
    return pImpl->mPort;
}

/// Server cert
void GRPCOptions::setServerCertificate(const std::string &cert)
{
    if (cert.empty())
    {
        throw std::invalid_argument("Server certificate is empty");
    }
    pImpl->mHaveServerCertificate = true;
    pImpl->mServerCertificate = cert;
}

std::optional<std::string> GRPCOptions::getServerCertificate() const noexcept
{
    return pImpl->mHaveServerCertificate ? 
           std::make_optional<std::string> (pImpl->mServerCertificate) :
           std::nullopt;
}

/// Client certs
void GRPCOptions::setClientKey(const std::string &key)
{   
    if (key.empty())
    {   
        throw std::invalid_argument("Client key is empty");
    }
    pImpl->mHaveClientKey = true;
    pImpl->mClientKey = key;
}

std::optional<std::string> GRPCOptions::getClientKey() const noexcept
{
    return pImpl->mHaveClientKey ? 
           std::make_optional<std::string> (pImpl->mClientKey) :
           std::nullopt;
}

void GRPCOptions::setClientCertificate(const std::string &cert)
{
    if (cert.empty())
    {   
        throw std::invalid_argument("Client certificate is empty");
    }   
    pImpl->mHaveClientCertificate = true;
    pImpl->mClientCertificate = cert;
}

std::optional<std::string> GRPCOptions::getClientCertificate() const noexcept
{
    return pImpl->mHaveClientCertificate ? 
           std::make_optional<std::string> (pImpl->mClientCertificate) :
           std::nullopt;
}

/// Access token
void GRPCOptions::setAccessToken(const std::string &token)
{
    if (token.empty()){throw std::invalid_argument("Token is empty");}
    pImpl->mHaveAccessToken = true;
    pImpl->mAccessToken = token;
}

std::optional<std::string> GRPCOptions::getAccessToken() const noexcept
{
    return pImpl->mHaveAccessToken ?
           std::make_optional<std::string> (pImpl->mAccessToken) : std::nullopt;
}


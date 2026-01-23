#ifndef USLINK_TO_DATA_PACKET_IMPORT_PROXY_GRPC_OPTIONS_HPP
#define USLINK_TO_DATA_PACKET_IMPORT_PROXY_GRPC_OPTIONS_HPP
#include <string>
#include <memory>
#include <filesystem>
#include <optional>
namespace USEEDLinkToDataPacketImportProxy
{
class GRPCOptions
{
public:
    /// @brief Constructor
    GRPCOptions();

    /// @brief Sets the host name - e.g., localhost or machine.domain.com.
    void setHost(const std::string &host);
    /// @result The host name.
    /// @note By default this is localhost. 
    [[nodiscard]] std::string getHost() const noexcept;
  
    /// @brief Sets the port number.
    void setPort(uint16_t port);
    /// @result The port.
    /// @note By default this is 50000.
    [[nodiscard]] uint16_t getPort() const noexcept;

    /// @brief Sets the API access token.
    void setAccessToken(const std::string &token);
    /// @result The access token.
    /// @note Access tokens can only be used by gRPC if the server
    ///       certificate is set.
    [[nodiscard]] std::optional<std::string> getAccessToken() const noexcept;

    /// @brief Sets the server's certificate.  This is public - e.g.,
    ///        localhost.crt.
    void setServerCertificate(const std::string &certificate);
    /// @result The server certificate.
    /// @note The server key must also be set for gRPC to use this.
    [[nodiscard]] std::optional<std::string> getServerCertificate() const noexcept;


    /// @brief Sets the client certificate for a full key exchange.
    void setClientCertificate(const std::string &certificate);
    /// @result The client ceritificate.
    [[nodiscard]] std::optional<std::string> getClientCertificate() const noexcept;

    /// @brief Sets the client's key.  This is private - e.g., localhost.key.
    void setClientKey(const std::string &key);
    /// @result The client key.
    /// @note The client key must also be set for gRPC to use this.
    [[nodiscard]] std::optional<std::string> getClientKey() const noexcept;

    /// @brief Destructor
    ~GRPCOptions();
    /// @brief Copy constructor.
    GRPCOptions(const GRPCOptions &options);
    /// @brief Move constructor.
    GRPCOptions(GRPCOptions &&options) noexcept;
    /// @brief Copy assignment.
    GRPCOptions& operator=(const GRPCOptions &options);
    /// @brief Move assignment.
    GRPCOptions& operator=(GRPCOptions &&options) noexcept;
private:
    class GRPCOptionsImpl;
    std::unique_ptr<GRPCOptionsImpl> pImpl;
};
/// @brief Convenience function to convert host and port to an address for gRPC.
[[nodiscard]] std::string makeAddress(const GRPCOptions &options);
}
#endif

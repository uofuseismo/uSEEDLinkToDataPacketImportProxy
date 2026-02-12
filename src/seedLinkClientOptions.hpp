#ifndef USEED_LINK_TO_DATA_PACKET_IMPORT_PROXY_SEED_LINK_CLIENT_OPTIONS_HPP
#define USEED_LINK_TO_DATA_PACKET_IMPORT_PROXY_SEED_LINK_CLIENT_OPTIONS_HPP
#include <memory>
#include <functional>
#include <filesystem>
#include <future>
namespace USEEDLinkToDataPacketImportProxy
{
 class StreamSelector;
}
namespace USEEDLinkToDataPacketImportProxy
{
/// @class SEEDLinkClientOptions "seedLinkClientOptions.hpp"
/// @brief Defines the SEEDLink client options.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class SEEDLinkClientOptions
{
public:
    /// @name Constructors
    /// @{

    /// @brief Constructor.
    SEEDLinkClientOptions();
    /// @brief Copy constructor.
    /// @param[in] options  The options from which to initialize this class.
    SEEDLinkClientOptions(const SEEDLinkClientOptions &options);
    /// @brief Move constructor.
    /// @param[in,out] options  The options from which to initialize this class.
    ///                         On exit, option's behavior is undefined.
    SEEDLinkClientOptions(SEEDLinkClientOptions &&options) noexcept;
    /// @}

    /// @name Operators
    /// @{

    /// @brief Copy assignment operator.
    /// @param[in] options  The options class to copy to this.
    /// @result A deep copy of the options.
    SEEDLinkClientOptions& operator=(const SEEDLinkClientOptions &options);
    /// @brief Move assignment operator.
    /// @param[in,out] options  The options class whose memory will be moved
    ///                         to this.  On exit, option's behavior is
    ///                         undefined.
    /// @result The memory from options moved to this.
    SEEDLinkClientOptions& operator=(SEEDLinkClientOptions &&options) noexcept;
    /// @}

    /// @name Properties
    /// @{

    /// @brief Sets the host of the SEEDLink server.
    /// @param[in] host  The address of the SEEDLink server.
    void setHost(const std::string &host);
    /// @result The hots address of the SEEDLink server.  By default this is
    ///         rtserve.iris.washington.edu
    [[nodiscard]] std::string getHost() const noexcept;

    /// @brief Sets the port number of the SEEDLink server.
    /// @param[in] port  The port of the server.
    void setPort(uint16_t port) noexcept;
    /// @result The port number of the SEEDLink server.  By default this 18000.
    [[nodiscard]] uint16_t getPort() const noexcept;

    /// @brief Sets the SEEDLink client's state file.  The state file
    ///        contains a list of sequence numbers written during
    ///        clean shutdown.  When the client resumes these numbers
    ///        are used to resume data streams.
    /// @param[in] stateFile  The path to the state file. 
    /// @throw std::runtime_error if the path to the state cannot be made.
    void setStateFile(const std::filesystem::path &stateFile);
    /// @result The path to the state file.
    /// @throws std::runtime_error if \c hasStateFile() is false.
    [[nodiscard]] std::filesystem::path getStateFile() const;
    /// @result True indicates the state file was set.
    [[nodiscard]] bool hasStateFile() const noexcept;
    /// @brief Controls the interval in which the state file is written.
    /// @param[in] interval   After this many packets are written the state
    ///                       file will be updated.
    void setStateFileUpdateInterval(uint16_t interval) noexcept;
    /// @result The state file update interval in packets.  The default is 100.
    [[nodiscard]] uint16_t getStateFileUpdateInterval() const noexcept;

    /// @result True indicates the state file will be deleted during shutdown. 
    /// @note This is feature only applies when \c hasStateFile() is true.
    /// @note This is useful when the state file should not be maintained but
    ///       should be resilient for reconnect scenarios.
    [[nodiscard]] bool deleteStateFileOnStop() const noexcept;
    /// @brief Setting this to true will result in the state file being deleted
    ///        on application shut down.
    void enableDeleteStateFileOnStop() noexcept;
    /// @brief Setting this to true will result in the state file being 
    ///        retained on application shut down.
    void disableDeleteStateFileOnStop() noexcept;

    /// @result True indicates the state file will be deleted prior to starting.
    /// @note This is feature only applies when \c hasStateFile() is true.
    /// @note This is useful in restart scenarios after a state file has
    ///       become corrupted.
    [[nodiscard]] bool deleteStateFileOnStart() const noexcept;
    /// @brief Setting this to true will result in the state file being deleted
    ///        when the application starts.
    void enableDeleteStateFileOnStart() noexcept;
    /// @brief Setting this to true will result in the state file being 
    ///        retained when the application starts.
    void disableDeleteStateFileOnStart() noexcept;

    /// @brief After this many seconds elapses the network the SEED Link
    ///        connection will be reset.
    /// @param[in] timeOut  The time out in seconds.  If this is 0
    ///                     then this will be disabled.
    void setNetworkTimeOut(const std::chrono::seconds &timeOut);
    /// @result After this many seconds the network connection be reset.
    ///         By default this is zero.
    [[nodiscard]] std::chrono::seconds getNetworkTimeOut() const noexcept;
    /// @brief The network reconnect delay in seconds.
    /// @param[in] delay  The network re-connect delay in seconds.
    void setNetworkReconnectDelay(const std::chrono::seconds &timeOut);
    /// @result The network re-connect delay in seconds.
    [[nodiscard]] std::chrono::seconds getNetworkReconnectDelay() const noexcept;

    /// @brief Adds a stream selector.
    /// @param[in] selector      The selector to add.
    /// @result True indicates the selector was added.
    /// @throws std::invalid_argument if the selector is not properly set.
    [[nodiscard]] bool addStreamSelector(const StreamSelector &selector);

    /// @result The stream selectors. 
    [[nodiscard]] std::vector<StreamSelector> getStreamSelectors() const noexcept;
    /// @}

    /// @name Destructors
    /// @{

    /// @brief Resets the class and releases memory.
    void clear() noexcept;
    /// @brief Destructor.
    ~SEEDLinkClientOptions();
    /// @}
private:
    class SEEDLinkClientOptionsImpl;
    std::unique_ptr<SEEDLinkClientOptionsImpl> pImpl;
};
}
#endif

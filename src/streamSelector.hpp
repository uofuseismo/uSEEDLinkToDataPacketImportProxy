#ifndef USEED_LINK_TO_DATA_PACKET_IMPORT_PROXY_STREAM_SELECTOR_HPP
#define USEED_LINK_TO_DATA_PACKET_IMPORT_PROXY_STREAM_SELECTOR_HPP
#include <chrono>
#include <memory>
namespace USEEDLinkToDataPacketImportProxy
{
/// @class StreamSelector "streamSelector.hpp"
/// @brief Defines the stream selector options used by the SEEDLink client.
/// @copyright Ben Baker (University of Utah) distributed under the MIT license.
class StreamSelector
{
public:
    /// @brief Defines the type of data to return.
    enum class Type
    {
        All,         /*!< Return everything. */
        Data,        /*!< Return data packet. */
        Event,       /*!< Return event packet. */
        Calibration, /*!< Return calibration packet. */
        Blockette,   /*!< Return blockette. */
        Timing,      /*!< Return timing packets. */
        Log          /*!< Return logs. */
    };
public:
    /// @name Constructors
    /// @{

    /// @brief Constructor.
    StreamSelector();
    /// @brief Copy constructor.
    /// @param[in] selector  The selector from which to initialize this class.
    StreamSelector(const StreamSelector &selector);
    /// @brief Move constructor.
    /// @param[in,out] selector  The selector from which to initialize this
    ///                          class.  On exit, selector's behavior is
    ///                          undefined.
    StreamSelector(StreamSelector &&selector) noexcept;
    /// @}

    /// @brief Sets the network selector.
    /// @param[in] network  The desired network code - e.g., UU.
    /// @throws std::invalid_argument if this is not a two-letter code.
    void setNetwork(const std::string &network);
    /// @result The desired network code.
    [[nodiscard]] std::string getNetwork() const;
    /// @result True indicates the network selector was set.
    [[nodiscard]] bool hasNetwork() const noexcept;

    /// @brief Sets the desired station.
    /// @param[in] station  The desired station code - e.g., FORK.
    /// @note If this is blank then all stations will be imported.
    void setStation(const std::string &station);
    /// @result The desired station name.
    [[nodiscard]] std::string getStation() const noexcept;

    /// @brief Sets the desired channels and location codes.
    /// @param[in] channel  The channel selector.  For example, this can be
    ///                     BH? to read all BH[1,2,Z] channels.  Or this
    ///                     can be HHZ to read only high-samplerate, high-gain
    ///                     vertical channels.  Or, if this is blank then
    ///                     all channels will be selected.  Or, this can be
    ///                     !L to exclude LCQ and LEP channels.  
    /// @param[in] type     The data type to be returned.
    /// @note This will select all channel codes.
    void setSelector(const std::string &channel,
                     const Type type = Type::Data);
    /// @brief Sets the desired channels and location codes.
    /// @param[in] channel  The channel selector.  For example, this can be
    ///                     BH? to read all BH[1,2,Z] channels.  Or this
    ///                     can be HHZ to read only high-samplerate, high-gain
    ///                     vertical channels.  Or, if this is blank then
    ///                     all channels will be selected.  Or, this can be
    ///                     !L to exclude LCQ and LEP channels.  
    /// @param[in] locationCode  The location code.  For example, this
    ///                          can be 01 to return only data with 01 location
    ///                          codes.  If this is empty then all location
    ///                          codes will be selected.
    /// @param[in] type          The data type to be returned.
    void setSelector(const std::string &channel,
                     const std::string &locationCode,
                     const Type type = Type::Data);
    /// @result A string representation of the selector.
    [[nodiscard]] std::string getSelector() const;

    /// @name Destructors
    /// @{

    /// @brief Resets the class.
    void clear() noexcept;
    /// @brief Destructor.
    ~StreamSelector();
    /// @}

    /// @name Operators
    /// @{

    /// @brief Copy assignment.
    /// @param[in] selector  The stream selector to copy to this.
    /// @result A deep copy of the input selector.
    StreamSelector& operator=(const StreamSelector &selector);
    /// @brief Move assignment.
    /// @param[in,out] selector  The stream selector whose memory will be
    ///                          moved to this.  On exit, selector's behavior
    ///                          is undefined.
    /// @result The memory from selector moved to this.
    StreamSelector& operator=(StreamSelector &&selector) noexcept;
    /// @}
public:
    class StreamSelectorImpl;
    std::unique_ptr<StreamSelectorImpl> pImpl;
};
}
#endif

#include "market_data/MarketDataEvent.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cmf
{
namespace
{

using Json = nlohmann::json;

const Json& requireField(const Json& object, const char* fieldName)
{
    if (!object.is_object() || !object.contains(fieldName))
    {
        throw std::runtime_error(std::string{"missing field: "} + fieldName);
    }
    return object.at(fieldName);
}

const Json& optionalField(const Json& object, const char* fieldName)
{
    static const Json nullValue;

    if (!object.is_object() || !object.contains(fieldName))
    {
        return nullValue;
    }
    return object.at(fieldName);
}

std::optional<std::string> getOptionalString(const Json& object, const char* fieldName)
{
    const Json& value = optionalField(object, fieldName);
    if (value.is_null())
    {
        return std::nullopt;
    }
    if (!value.is_string())
    {
        throw std::runtime_error(std::string{fieldName} + " must be a string");
    }
    return value.get<std::string>();
}

std::string getRequiredString(const Json& object, const char* fieldName)
{
    const Json& value = requireField(object, fieldName);
    if (!value.is_string())
    {
        throw std::runtime_error(std::string{fieldName} + " must be a string");
    }
    return value.get<std::string>();
}

std::string getStringOrNumber(const Json& object, const char* fieldName)
{
    const Json& value = requireField(object, fieldName);
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_number_unsigned())
    {
        return std::to_string(value.get<std::uint64_t>());
    }
    if (value.is_number_integer())
    {
        return std::to_string(value.get<std::int64_t>());
    }
    throw std::runtime_error(std::string{fieldName} + " must be a string or integer");
}

template <typename T>
T getInteger(const Json& object, const char* fieldName)
{
    const Json& value = requireField(object, fieldName);
    if (!value.is_number_integer())
    {
        throw std::runtime_error(std::string{fieldName} + " must be an integer");
    }
    return value.get<T>();
}

char getChar(const Json& object, const char* fieldName)
{
    const std::string value = getRequiredString(object, fieldName);
    if (value.empty())
    {
        throw std::runtime_error(std::string{fieldName} + " must not be empty");
    }
    return value.front();
}

std::optional<std::int32_t> getOptionalInt32(const Json& object, const char* fieldName)
{
    const Json& value = optionalField(object, fieldName);
    if (value.is_null())
    {
        return std::nullopt;
    }
    if (!value.is_number_integer())
    {
        throw std::runtime_error(std::string{fieldName} + " must be a 32-bit signed integer");
    }
    return value.get<std::int32_t>();
}

std::int64_t scaleDecimalPrice(double price)
{
    return static_cast<std::int64_t>(std::llround(price * static_cast<double>(MarketDataEvent::PriceScale)));
}

std::optional<std::int64_t> getScaledPrice(const Json& object, const char* fieldName, std::optional<double>& decimalPrice)
{
    const Json& value = requireField(object, fieldName);
    if (value.is_null())
    {
        decimalPrice = std::nullopt;
        return std::nullopt;
    }

    if (value.is_number_integer())
    {
        const auto scaled = value.get<std::int64_t>();
        if (scaled == std::numeric_limits<std::int64_t>::max())
        {
            decimalPrice = std::nullopt;
            return std::nullopt;
        }
        decimalPrice = static_cast<double>(scaled) / static_cast<double>(MarketDataEvent::PriceScale);
        return scaled;
    }

    double parsed = 0.0;
    if (value.is_number_float())
    {
        parsed = value.get<double>();
    }
    else if (value.is_string())
    {
        const std::string priceString = value.get<std::string>();
        if (priceString.empty())
        {
            decimalPrice = std::nullopt;
            return std::nullopt;
        }

        std::size_t consumed = 0;
        parsed = std::stod(priceString, &consumed);
        if (consumed != priceString.size())
        {
            throw std::runtime_error(std::string{fieldName} + " must be a decimal number");
        }
    }
    else
    {
        throw std::runtime_error(std::string{fieldName} + " must be a number, string, or null");
    }

    decimalPrice = parsed;
    return scaleDecimalPrice(parsed);
}

} // namespace

const std::string& MarketDataEvent::timestamp() const
{
    static const std::string empty;
    if (tsEvent.has_value())
    {
        return *tsEvent;
    }
    return empty;
}

MarketDataEvent MarketDataEvent::fromJson(std::string_view jsonString)
{
    const Json message = Json::parse(jsonString);
    const Json& header = requireField(message, "hd");

    MarketDataEvent event;
    event.tsRecv = getOptionalString(message, "ts_recv");
    event.tsEvent = getRequiredString(header, "ts_event");

    event.rtype = getInteger<int>(header, "rtype");
    event.publisherId = getInteger<int>(header, "publisher_id");
    event.instrumentId = getInteger<int>(header, "instrument_id");

    event.action = getChar(message, "action");
    event.side = getChar(message, "side");

    event.scaledPrice = getScaledPrice(message, "price", event.price);
    event.size = getInteger<std::int64_t>(message, "size");
    event.channelId = getInteger<int>(message, "channel_id");
    event.orderId = getStringOrNumber(message, "order_id");

    event.flags = getInteger<int>(message, "flags");
    event.tsInDelta = getOptionalInt32(message, "ts_in_delta");
    event.sequence = getInteger<std::uint64_t>(message, "sequence");

    event.symbol = getRequiredString(message, "symbol");

    return event;
}

} // namespace cmf

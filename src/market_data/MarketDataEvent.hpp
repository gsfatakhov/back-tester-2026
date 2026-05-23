#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cmf
{

struct MarketDataEvent
{
    static constexpr std::int64_t PriceScale = 1'000'000'000LL;

    std::optional<std::string> tsRecv;
    std::optional<std::string> tsEvent;

    int rtype = 0;
    int publisherId = 0;
    int instrumentId = 0;

    char action = 'N';
    char side = 'N';

    std::optional<double> price;
    std::optional<std::int64_t> scaledPrice;
    std::int64_t size = 0;

    int channelId = 0;
    std::string orderId;

    int flags = 0;
    std::optional<std::int32_t> tsInDelta;
    std::uint64_t sequence = 0;

    std::string symbol;

    [[nodiscard]] const std::string& timestamp() const;

    static MarketDataEvent fromJson(std::string_view jsonString);
};

} // namespace cmf

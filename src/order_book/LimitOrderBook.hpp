#pragma once

#include "market_data/MarketDataEvent.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cmf
{

struct PriceLevel
{
    std::int64_t price = 0;
    std::int64_t quantity = 0;
};

class LimitOrderBook
{
  public:
    void apply(const MarketDataEvent& event);
    void clear();

    [[nodiscard]] bool containsOrder(std::string_view orderId) const;
    [[nodiscard]] std::optional<PriceLevel> bestBid() const;
    [[nodiscard]] std::optional<PriceLevel> bestAsk() const;
    [[nodiscard]] std::int64_t volumeAt(char side, std::int64_t price) const;

    [[nodiscard]] std::size_t orderCount() const { return orders_.size(); }
    [[nodiscard]] std::size_t bidLevelCount() const { return bids_.size(); }
    [[nodiscard]] std::size_t askLevelCount() const { return asks_.size(); }

    void printSnapshot(std::ostream& output, std::size_t depth = 5) const;

    static double toDecimalPrice(std::int64_t scaledPrice);

  private:
    struct OrderState
    {
        char side = 'N';
        std::int64_t price = 0;
        std::int64_t size = 0;
    };

    using BidLevels = std::map<std::int64_t, std::int64_t, std::greater<std::int64_t>>;
    using AskLevels = std::map<std::int64_t, std::int64_t>;

    std::unordered_map<std::string, OrderState> orders_;
    BidLevels bids_;
    AskLevels asks_;

    void addOrder(std::string orderId, OrderState order);
    void removeOrder(const std::string& orderId);
    void reduceOrder(const std::string& orderId, std::int64_t quantity);

    void addLevelQuantity(char side, std::int64_t price, std::int64_t quantity);
    void removeLevelQuantity(char side, std::int64_t price, std::int64_t quantity);
};

} // namespace cmf

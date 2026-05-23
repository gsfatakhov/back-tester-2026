#include "order_book/LimitOrderBook.hpp"

#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace cmf
{
namespace
{

bool isBidSide(char side)
{
    return side == 'B';
}

bool isAskSide(char side)
{
    return side == 'A';
}

bool isBookSide(char side)
{
    return isBidSide(side) || isAskSide(side);
}

} // namespace

void LimitOrderBook::apply(const MarketDataEvent& event)
{
    switch (event.action)
    {
    case 'A':
        if (event.orderId.empty() || !isBookSide(event.side) || !event.scaledPrice.has_value() || event.size <= 0)
        {
            return;
        }
        if (containsOrder(event.orderId))
        {
            removeOrder(event.orderId);
        }
        addOrder(event.orderId, {event.side, event.scaledPrice.value(), event.size});
        break;

    case 'M':
    {
        if (event.orderId.empty())
        {
            return;
        }

        const auto existing = orders_.find(event.orderId);
        const char side = isBookSide(event.side)
                              ? event.side
                              : existing != orders_.end() ? existing->second.side : 'N';

        if (existing != orders_.end())
        {
            removeOrder(event.orderId);
        }

        if (isBookSide(side) && event.scaledPrice.has_value() && event.size > 0)
        {
            addOrder(event.orderId, {side, event.scaledPrice.value(), event.size});
        }
        break;
    }

    case 'C':
        if (!event.orderId.empty() && containsOrder(event.orderId))
        {
            if (event.size <= 0)
            {
                removeOrder(event.orderId);
            }
            else
            {
                reduceOrder(event.orderId, event.size);
            }
        }
        break;

    case 'R':
        clear();
        break;

    case 'T':
    case 'F':
    case 'N':
        break;

    default:
        throw std::runtime_error(std::string{"unknown order book action: "} + event.action);
    }
}

void LimitOrderBook::clear()
{
    orders_.clear();
    bids_.clear();
    asks_.clear();
}

bool LimitOrderBook::containsOrder(std::string_view orderId) const
{
    return orders_.find(std::string{orderId}) != orders_.end();
}

std::optional<PriceLevel> LimitOrderBook::bestBid() const
{
    if (bids_.empty())
    {
        return std::nullopt;
    }
    return PriceLevel{bids_.begin()->first, bids_.begin()->second};
}

std::optional<PriceLevel> LimitOrderBook::bestAsk() const
{
    if (asks_.empty())
    {
        return std::nullopt;
    }
    return PriceLevel{asks_.begin()->first, asks_.begin()->second};
}

std::int64_t LimitOrderBook::volumeAt(char side, std::int64_t price) const
{
    if (isBidSide(side))
    {
        const auto level = bids_.find(price);
        return level == bids_.end() ? 0 : level->second;
    }
    if (isAskSide(side))
    {
        const auto level = asks_.find(price);
        return level == asks_.end() ? 0 : level->second;
    }
    return 0;
}

void LimitOrderBook::printSnapshot(std::ostream& output, std::size_t depth) const
{
    output << "Bids:\n";
    std::size_t printed = 0;
    for (const auto& [price, quantity] : bids_)
    {
        if (printed++ == depth)
        {
            break;
        }
        output << "  " << std::fixed << std::setprecision(9) << toDecimalPrice(price) << " x " << quantity << '\n';
    }

    output << "Asks:\n";
    printed = 0;
    for (const auto& [price, quantity] : asks_)
    {
        if (printed++ == depth)
        {
            break;
        }
        output << "  " << std::fixed << std::setprecision(9) << toDecimalPrice(price) << " x " << quantity << '\n';
    }
}

double LimitOrderBook::toDecimalPrice(std::int64_t scaledPrice)
{
    return static_cast<double>(scaledPrice) / static_cast<double>(MarketDataEvent::PriceScale);
}

void LimitOrderBook::addOrder(std::string orderId, OrderState order)
{
    addLevelQuantity(order.side, order.price, order.size);
    orders_.emplace(std::move(orderId), order);
}

void LimitOrderBook::removeOrder(const std::string& orderId)
{
    const auto order = orders_.find(orderId);
    if (order == orders_.end())
    {
        return;
    }

    removeLevelQuantity(order->second.side, order->second.price, order->second.size);
    orders_.erase(order);
}

void LimitOrderBook::reduceOrder(const std::string& orderId, std::int64_t quantity)
{
    const auto order = orders_.find(orderId);
    if (order == orders_.end())
    {
        return;
    }

    if (quantity >= order->second.size)
    {
        removeOrder(orderId);
        return;
    }

    order->second.size -= quantity;
    removeLevelQuantity(order->second.side, order->second.price, quantity);
}

void LimitOrderBook::addLevelQuantity(char side, std::int64_t price, std::int64_t quantity)
{
    if (quantity <= 0)
    {
        return;
    }

    if (isBidSide(side))
    {
        bids_[price] += quantity;
    }
    else if (isAskSide(side))
    {
        asks_[price] += quantity;
    }
}

void LimitOrderBook::removeLevelQuantity(char side, std::int64_t price, std::int64_t quantity)
{
    if (quantity <= 0)
    {
        return;
    }

    auto removeFrom = [&](auto& levels) {
        const auto level = levels.find(price);
        if (level == levels.end())
        {
            return;
        }

        if (level->second <= quantity)
        {
            levels.erase(level);
        }
        else
        {
            level->second -= quantity;
        }
    };

    if (isBidSide(side))
    {
        removeFrom(bids_);
    }
    else if (isAskSide(side))
    {
        removeFrom(asks_);
    }
}

} // namespace cmf

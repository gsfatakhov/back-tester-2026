#include "order_book/LimitOrderBook.hpp"

#include "catch2/catch_all.hpp"

#include <string>
#include <utility>

using namespace cmf;

namespace
{

MarketDataEvent event(
    char action,
    std::string orderId,
    char side,
    std::int64_t price,
    std::int64_t size)
{
    MarketDataEvent result;
    result.action = action;
    result.orderId = std::move(orderId);
    result.side = side;
    result.scaledPrice = price;
    result.price = LimitOrderBook::toDecimalPrice(price);
    result.size = size;
    result.instrumentId = 1;
    result.tsEvent = "2026-04-09T09:00:00.000000000Z";
    return result;
}

} // namespace

TEST_CASE("LimitOrderBook maintains L2 aggregates from L3 events", "[LimitOrderBook]")
{
    LimitOrderBook book;

    book.apply(event('A', "bid-1", 'B', 1000000000, 10));
    book.apply(event('A', "bid-2", 'B', 1000000000, 5));
    book.apply(event('A', "ask-1", 'A', 1010000000, 7));

    REQUIRE(book.bestBid()->price == 1000000000);
    REQUIRE(book.bestBid()->quantity == 15);
    REQUIRE(book.bestAsk()->price == 1010000000);
    REQUIRE(book.bestAsk()->quantity == 7);
    REQUIRE(book.volumeAt('B', 1000000000) == 15);

    book.apply(event('M', "bid-1", 'B', 1020000000, 6));

    REQUIRE(book.volumeAt('B', 1000000000) == 5);
    REQUIRE(book.bestBid()->price == 1020000000);
    REQUIRE(book.bestBid()->quantity == 6);

    book.apply(event('C', "bid-1", 'B', 1020000000, 2));

    REQUIRE(book.bestBid()->price == 1020000000);
    REQUIRE(book.bestBid()->quantity == 4);

    book.apply(event('C', "bid-1", 'B', 1020000000, 4));

    REQUIRE(book.bestBid()->price == 1000000000);
    REQUIRE(book.bestBid()->quantity == 5);
    REQUIRE_FALSE(book.containsOrder("bid-1"));
}

TEST_CASE("LimitOrderBook treats trade and fill as state no-ops", "[LimitOrderBook]")
{
    LimitOrderBook book;
    book.apply(event('A', "ask-1", 'A', 1010000000, 7));

    book.apply(event('T', "trade", 'B', 1010000000, 3));
    book.apply(event('F', "ask-1", 'A', 1010000000, 3));

    REQUIRE(book.bestAsk()->price == 1010000000);
    REQUIRE(book.bestAsk()->quantity == 7);
}

TEST_CASE("LimitOrderBook clears all state", "[LimitOrderBook]")
{
    LimitOrderBook book;
    book.apply(event('A', "bid-1", 'B', 1000000000, 10));
    book.apply(event('A', "ask-1", 'A', 1010000000, 7));

    MarketDataEvent clearEvent;
    clearEvent.action = 'R';
    book.apply(clearEvent);

    REQUIRE_FALSE(book.bestBid().has_value());
    REQUIRE_FALSE(book.bestAsk().has_value());
    REQUIRE(book.orderCount() == 0);
}

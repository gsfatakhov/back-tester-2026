#include "market_data/MarketDataEvent.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;

namespace
{

const char* ValidJson = R"json(
{
  "ts_recv": "2026-04-09T09:00:00.000000001Z",
  "hd": {
    "ts_event": "2026-04-09T09:00:00.000000000Z",
    "rtype": 160,
    "publisher_id": 12,
    "instrument_id": 42
  },
  "action": "A",
  "side": "B",
  "price": "1.234567890",
  "size": 10,
  "channel_id": 2,
  "order_id": "abc",
  "flags": 128,
  "ts_in_delta": null,
  "sequence": 7,
  "symbol": "EUR/USD"
}
)json";

} // namespace

TEST_CASE("MarketDataEvent parses Databento NDJSON fields", "[MarketDataEvent]")
{
    const auto event = MarketDataEvent::fromJson(ValidJson);

    REQUIRE(event.tsRecv == "2026-04-09T09:00:00.000000001Z");
    REQUIRE(event.tsEvent == "2026-04-09T09:00:00.000000000Z");
    REQUIRE(event.rtype == 160);
    REQUIRE(event.publisherId == 12);
    REQUIRE(event.instrumentId == 42);
    REQUIRE(event.action == 'A');
    REQUIRE(event.side == 'B');
    REQUIRE(event.price.value() == Catch::Approx(1.234567890));
    REQUIRE(event.scaledPrice == 1234567890);
    REQUIRE(event.size == 10);
    REQUIRE(event.orderId == "abc");
    REQUIRE_FALSE(event.tsInDelta.has_value());
    REQUIRE(event.sequence == 7);
    REQUIRE(event.symbol == "EUR/USD");
}

TEST_CASE("MarketDataEvent handles null price and integer ts_in_delta", "[MarketDataEvent]")
{
    const auto event = MarketDataEvent::fromJson(R"json(
{
  "ts_recv": null,
  "hd": {
    "ts_event": "2026-04-09T09:00:01.000000000Z",
    "rtype": 160,
    "publisher_id": 12,
    "instrument_id": 42
  },
  "action": "C",
  "side": "B",
  "price": null,
  "size": 3,
  "channel_id": 2,
  "order_id": 12345,
  "flags": 128,
  "ts_in_delta": -12,
  "sequence": 8,
  "symbol": "EUR/USD"
}
)json");

    REQUIRE_FALSE(event.tsRecv.has_value());
    REQUIRE_FALSE(event.price.has_value());
    REQUIRE_FALSE(event.scaledPrice.has_value());
    REQUIRE(event.tsInDelta == -12);
    REQUIRE(event.orderId == "12345");
}

TEST_CASE("MarketDataEvent rejects invalid field types", "[MarketDataEvent]")
{
    REQUIRE_THROWS_AS(MarketDataEvent::fromJson(R"json({"hd": {}})json"), std::runtime_error);
    REQUIRE_THROWS_AS(MarketDataEvent::fromJson(R"json(
{
  "hd": {
    "ts_event": "2026-04-09T09:00:00.000000000Z",
    "rtype": 160,
    "publisher_id": 12,
    "instrument_id": 42
  },
  "action": "A",
  "side": "B",
  "price": "12.bad",
  "size": 10,
  "channel_id": 2,
  "order_id": "abc",
  "flags": 128,
  "ts_in_delta": null,
  "sequence": 7,
  "symbol": "EUR/USD"
}
)json"),
                      std::runtime_error);
}

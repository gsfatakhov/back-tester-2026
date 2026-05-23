#include "market_data/EventPipeline.hpp"
#include "order_book/BookDispatcher.hpp"

#include "catch2/catch_all.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cmf;

namespace
{

std::string jsonLine(
    std::string ts,
    std::uint64_t sequence,
    int instrumentId,
    std::string orderId,
    char action,
    char side,
    const char* price,
    std::int64_t size)
{
    return R"json({"ts_recv":")json" + ts + R"json(","hd":{"ts_event":")json" + ts + R"json(","rtype":160,"publisher_id":12,"instrument_id":)json" + std::to_string(instrumentId) + R"json(},"action":")json" + action + R"json(","side":")json" + side + R"json(","price":)json" + price + R"json(,"size":)json" + std::to_string(size) + R"json(,"channel_id":2,"order_id":")json" + orderId + R"json(","flags":128,"ts_in_delta":null,"sequence":)json" + std::to_string(sequence) + R"json(,"symbol":"EUR/USD"})json";
}

void writeFile(const std::filesystem::path& path, const std::vector<std::string>& lines)
{
    std::ofstream output{path};
    for (const auto& line : lines)
    {
        output << line << '\n';
    }
}

std::filesystem::path makeTempDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "backtester-pipeline-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("Flat and hierarchy mergers produce the same chronological stream", "[Pipeline]")
{
    const auto dir = makeTempDir();
    writeFile(
        dir / "a.mbo.json",
        {
            jsonLine("2026-04-09T09:00:00.000000000Z", 1, 1, "a1", 'A', 'B', "\"1.000000000\"", 10),
            jsonLine("2026-04-09T09:00:02.000000000Z", 3, 1, "a2", 'A', 'A', "\"1.020000000\"", 5),
        });
    writeFile(
        dir / "b.mbo.json",
        {
            jsonLine("2026-04-09T09:00:01.000000000Z", 2, 2, "b1", 'A', 'B', "\"2.000000000\"", 4),
            jsonLine("2026-04-09T09:00:03.000000000Z", 4, 2, "b2", 'A', 'A', "\"2.020000000\"", 6),
        });

    const auto files = listMarketDataFiles(dir);
    const auto flat = collectMergedEvents(files, MergeStrategy::Flat);
    const auto hierarchy = collectMergedEvents(files, MergeStrategy::Hierarchy);

    REQUIRE(flat.size() == 4);
    REQUIRE(hierarchy.size() == 4);
    for (std::size_t i = 0; i < flat.size(); ++i)
    {
        REQUIRE(flat[i].timestamp() == hierarchy[i].timestamp());
        REQUIRE(flat[i].sequence == hierarchy[i].sequence);
    }

    REQUIRE(flat[0].orderId == "a1");
    REQUIRE(flat[1].orderId == "b1");
    REQUIRE(flat[2].orderId == "a2");
    REQUIRE(flat[3].orderId == "b2");

    std::filesystem::remove_all(dir);
}

TEST_CASE("BookDispatcher routes events and falls back from order id to instrument", "[Pipeline]")
{
    const auto dir = makeTempDir();
    const auto file = dir / "single.mbo.json";
    writeFile(
        file,
        {
            jsonLine("2026-04-09T09:00:00.000000000Z", 1, 1, "bid-1", 'A', 'B', "\"1.000000000\"", 10),
            jsonLine("2026-04-09T09:00:01.000000000Z", 2, 1, "ask-1", 'A', 'A', "\"1.010000000\"", 7),
            jsonLine("2026-04-09T09:00:02.000000000Z", 3, 2, "ask-2", 'A', 'A', "\"2.010000000\"", 5),
            jsonLine("2026-04-09T09:00:03.000000000Z", 4, 0, "bid-1", 'C', 'B', "null", 3),
        });

    BookDispatcher dispatcher;
    const auto result = processSingleFile(
        file,
        [&](const MarketDataEvent& event) {
            dispatcher.process(event);
        });

    REQUIRE(result.stats.totalEvents == 4);

    const LimitOrderBook* book1 = dispatcher.findBook(1);
    REQUIRE(book1 != nullptr);
    REQUIRE(book1->bestBid()->price == 1000000000);
    REQUIRE(book1->bestBid()->quantity == 7);
    REQUIRE(book1->bestAsk()->price == 1010000000);
    REQUIRE(book1->bestAsk()->quantity == 7);

    const LimitOrderBook* book2 = dispatcher.findBook(2);
    REQUIRE(book2 != nullptr);
    REQUIRE(book2->bestAsk()->price == 2010000000);
    REQUIRE(book2->bestAsk()->quantity == 5);

    std::filesystem::remove_all(dir);
}

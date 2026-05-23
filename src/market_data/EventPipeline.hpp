#pragma once

#include "market_data/MarketDataEvent.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace cmf
{

enum class MergeStrategy
{
    Flat,
    Hierarchy
};

struct PipelineStats
{
    std::uint64_t totalEvents = 0;
    double seconds = 0.0;
    double throughput = 0.0;
};

struct DispatchResult
{
    PipelineStats stats;
    std::vector<MarketDataEvent> firstEvents;
    std::vector<MarketDataEvent> lastEvents;
    std::string firstTimestamp;
    std::string lastTimestamp;
};

using EventConsumer = std::function<void(const MarketDataEvent&)>;

MergeStrategy parseMergeStrategy(std::string_view value);
const char* mergeStrategyName(MergeStrategy strategy);

std::vector<std::filesystem::path> listMarketDataFiles(const std::filesystem::path& folder);

DispatchResult processSingleFile(
    const std::filesystem::path& file,
    const EventConsumer& consumer,
    std::size_t captureLimit = 10);

DispatchResult processMergedFiles(
    const std::vector<std::filesystem::path>& files,
    MergeStrategy strategy,
    const EventConsumer& consumer,
    std::size_t captureLimit = 10);

std::vector<MarketDataEvent> collectMergedEvents(
    const std::vector<std::filesystem::path>& files,
    MergeStrategy strategy);

} // namespace cmf

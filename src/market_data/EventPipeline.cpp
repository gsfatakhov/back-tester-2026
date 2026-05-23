#include "market_data/EventPipeline.hpp"

#include "market_data/BlockingQueue.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>

namespace cmf
{
namespace
{

constexpr std::size_t QueueCapacity = 4096;

using EventQueue = BlockingQueue<MarketDataEvent>;
using EventQueuePtr = std::shared_ptr<EventQueue>;

class ErrorState
{
  public:
    void capture()
    {
        std::lock_guard lock{mutex_};
        if (error_ == nullptr)
        {
            error_ = std::current_exception();
        }
    }

    void rethrowIfAny()
    {
        if (error_ != nullptr)
        {
            std::rethrow_exception(error_);
        }
    }

  private:
    std::mutex mutex_;
    std::exception_ptr error_ = nullptr;
};

bool earlierEvent(const MarketDataEvent& lhs, const MarketDataEvent& rhs)
{
    if (lhs.timestamp() != rhs.timestamp())
    {
        return lhs.timestamp() < rhs.timestamp();
    }
    return lhs.sequence <= rhs.sequence;
}

void captureEvent(DispatchResult& result, const MarketDataEvent& event, std::size_t captureLimit)
{
    if (result.stats.totalEvents == 0)
    {
        result.firstTimestamp = event.timestamp();
    }
    result.lastTimestamp = event.timestamp();

    if (result.firstEvents.size() < captureLimit)
    {
        result.firstEvents.push_back(event);
    }

    result.lastEvents.push_back(event);
    if (result.lastEvents.size() > captureLimit)
    {
        result.lastEvents.erase(result.lastEvents.begin());
    }

    ++result.stats.totalEvents;
}

void produceFile(const std::filesystem::path& path, const EventQueuePtr& output)
{
    std::ifstream input{path};
    if (!input.is_open())
    {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    std::string line;
    std::uint64_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (line.empty())
        {
            continue;
        }

        try
        {
            output->push(MarketDataEvent::fromJson(line));
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }
}

std::vector<EventQueuePtr> makeQueues(std::size_t count)
{
    std::vector<EventQueuePtr> queues;
    queues.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        queues.push_back(std::make_shared<EventQueue>(QueueCapacity));
    }
    return queues;
}

std::vector<std::thread> startProducers(
    const std::vector<std::filesystem::path>& files,
    const std::vector<EventQueuePtr>& outputs,
    ErrorState& errors)
{
    std::vector<std::thread> producers;
    producers.reserve(files.size());

    for (std::size_t i = 0; i < files.size(); ++i)
    {
        producers.emplace_back([&, i]() {
            try
            {
                produceFile(files[i], outputs[i]);
            }
            catch (...)
            {
                errors.capture();
            }
            outputs[i]->close();
        });
    }

    return producers;
}

void joinAll(std::vector<std::thread>& threads)
{
    for (auto& thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void flatMergeQueues(const std::vector<EventQueuePtr>& inputs, const EventQueuePtr& output)
{
    struct QueueItem
    {
        MarketDataEvent event;
        std::size_t inputIndex = 0;
    };

    struct Compare
    {
        bool operator()(const QueueItem& lhs, const QueueItem& rhs) const
        {
            if (lhs.event.timestamp() != rhs.event.timestamp())
            {
                return lhs.event.timestamp() > rhs.event.timestamp();
            }
            if (lhs.event.sequence != rhs.event.sequence)
            {
                return lhs.event.sequence > rhs.event.sequence;
            }
            return lhs.inputIndex > rhs.inputIndex;
        }
    };

    std::priority_queue<QueueItem, std::vector<QueueItem>, Compare> pending;
    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        MarketDataEvent event;
        if (inputs[i]->pop(event))
        {
            pending.push({std::move(event), i});
        }
    }

    while (!pending.empty())
    {
        QueueItem current = pending.top();
        pending.pop();

        const std::size_t inputIndex = current.inputIndex;
        output->push(std::move(current.event));

        MarketDataEvent nextEvent;
        if (inputs[inputIndex]->pop(nextEvent))
        {
            pending.push({std::move(nextEvent), inputIndex});
        }
    }

    output->close();
}

void mergeTwoQueues(const EventQueuePtr& left, const EventQueuePtr& right, const EventQueuePtr& output)
{
    MarketDataEvent leftEvent;
    MarketDataEvent rightEvent;
    bool hasLeft = left->pop(leftEvent);
    bool hasRight = right->pop(rightEvent);

    while (hasLeft && hasRight)
    {
        if (earlierEvent(leftEvent, rightEvent))
        {
            output->push(std::move(leftEvent));
            hasLeft = left->pop(leftEvent);
        }
        else
        {
            output->push(std::move(rightEvent));
            hasRight = right->pop(rightEvent);
        }
    }

    while (hasLeft)
    {
        output->push(std::move(leftEvent));
        hasLeft = left->pop(leftEvent);
    }

    while (hasRight)
    {
        output->push(std::move(rightEvent));
        hasRight = right->pop(rightEvent);
    }

    output->close();
}

EventQueuePtr startHierarchyMergers(std::vector<EventQueuePtr> currentLevel, std::vector<std::thread>& mergers)
{
    while (currentLevel.size() > 1)
    {
        std::vector<EventQueuePtr> nextLevel;

        for (std::size_t i = 0; i < currentLevel.size(); i += 2)
        {
            if (i + 1 == currentLevel.size())
            {
                nextLevel.push_back(currentLevel[i]);
                continue;
            }

            auto output = std::make_shared<EventQueue>(QueueCapacity);
            mergers.emplace_back(mergeTwoQueues, currentLevel[i], currentLevel[i + 1], output);
            nextLevel.push_back(output);
        }

        currentLevel = std::move(nextLevel);
    }

    return currentLevel.front();
}

void dispatchEvents(
    const EventQueuePtr& input,
    const EventConsumer& consumer,
    DispatchResult& result,
    std::size_t captureLimit)
{
    MarketDataEvent event;
    while (input->pop(event))
    {
        captureEvent(result, event, captureLimit);
        if (consumer)
        {
            consumer(event);
        }
    }
}

void finishStats(DispatchResult& result, std::chrono::steady_clock::time_point start)
{
    const auto finish = std::chrono::steady_clock::now();
    result.stats.seconds = std::chrono::duration<double>(finish - start).count();
    result.stats.throughput = result.stats.seconds > 0.0
                                  ? static_cast<double>(result.stats.totalEvents) / result.stats.seconds
                                  : 0.0;
}

} // namespace

MergeStrategy parseMergeStrategy(std::string_view value)
{
    if (value == "flat")
    {
        return MergeStrategy::Flat;
    }
    if (value == "hierarchy")
    {
        return MergeStrategy::Hierarchy;
    }
    throw std::runtime_error("merge strategy must be flat, hierarchy, or both");
}

const char* mergeStrategyName(MergeStrategy strategy)
{
    switch (strategy)
    {
    case MergeStrategy::Flat:
        return "flat";
    case MergeStrategy::Hierarchy:
        return "hierarchy";
    }
    return "unknown";
}

std::vector<std::filesystem::path> listMarketDataFiles(const std::filesystem::path& folder)
{
    if (!std::filesystem::is_directory(folder))
    {
        throw std::runtime_error("not a directory: " + folder.string());
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        const auto path = entry.path();
        const auto filename = path.filename().string();
        if (entry.is_regular_file() && filename.ends_with(".mbo.json"))
        {
            files.push_back(path);
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

DispatchResult processSingleFile(
    const std::filesystem::path& file,
    const EventConsumer& consumer,
    std::size_t captureLimit)
{
    std::ifstream input{file};
    if (!input.is_open())
    {
        throw std::runtime_error("failed to open file: " + file.string());
    }

    DispatchResult result;
    const auto start = std::chrono::steady_clock::now();

    std::string line;
    std::uint64_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (line.empty())
        {
            continue;
        }

        try
        {
            MarketDataEvent event = MarketDataEvent::fromJson(line);
            captureEvent(result, event, captureLimit);
            if (consumer)
            {
                consumer(event);
            }
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(file.string() + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }

    finishStats(result, start);
    return result;
}

DispatchResult processMergedFiles(
    const std::vector<std::filesystem::path>& files,
    MergeStrategy strategy,
    const EventConsumer& consumer,
    std::size_t captureLimit)
{
    if (files.empty())
    {
        throw std::runtime_error("no .mbo.json files to process");
    }

    ErrorState errors;
    auto inputQueues = makeQueues(files.size());
    std::vector<std::thread> mergerThreads;
    EventQueuePtr outputQueue;

    DispatchResult result;
    const auto start = std::chrono::steady_clock::now();

    auto producers = startProducers(files, inputQueues, errors);

    if (strategy == MergeStrategy::Flat)
    {
        outputQueue = std::make_shared<EventQueue>(QueueCapacity);
        mergerThreads.emplace_back(flatMergeQueues, inputQueues, outputQueue);
    }
    else
    {
        outputQueue = startHierarchyMergers(inputQueues, mergerThreads);
    }

    std::thread dispatcher{dispatchEvents, outputQueue, std::cref(consumer), std::ref(result), captureLimit};

    joinAll(producers);
    joinAll(mergerThreads);
    dispatcher.join();
    errors.rethrowIfAny();

    finishStats(result, start);
    return result;
}

std::vector<MarketDataEvent> collectMergedEvents(
    const std::vector<std::filesystem::path>& files,
    MergeStrategy strategy)
{
    std::vector<MarketDataEvent> events;
    processMergedFiles(
        files,
        strategy,
        [&](const MarketDataEvent& event) {
            events.push_back(event);
        },
        0);
    return events;
}

} // namespace cmf

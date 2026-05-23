#pragma once

#include "market_data/BlockingQueue.hpp"
#include "market_data/MarketDataEvent.hpp"
#include "order_book/LimitOrderBook.hpp"

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <string>
#include <thread>
#include <unordered_map>

namespace cmf
{

class AsyncSnapshotPrinter
{
  public:
    explicit AsyncSnapshotPrinter(std::ostream& output);
    ~AsyncSnapshotPrinter();

    AsyncSnapshotPrinter(const AsyncSnapshotPrinter&) = delete;
    AsyncSnapshotPrinter& operator=(const AsyncSnapshotPrinter&) = delete;

    void submit(std::string snapshot);
    void close();

  private:
    BlockingQueue<std::string> snapshots_;
    std::ostream& output_;
    std::thread worker_;
    bool closed_ = false;
};

class BookDispatcher
{
  public:
    using SnapshotSink = std::function<void(std::string)>;

    explicit BookDispatcher(std::size_t snapshotEvery = 0, SnapshotSink snapshotSink = {});

    void process(const MarketDataEvent& event);

    [[nodiscard]] const std::unordered_map<int, LimitOrderBook>& books() const { return books_; }
    [[nodiscard]] const LimitOrderBook* findBook(int instrumentId) const;
    [[nodiscard]] std::uint64_t processedEvents() const { return processedEvents_; }

    void printFinalBest(std::ostream& output) const;

  private:
    std::size_t snapshotEvery_ = 0;
    SnapshotSink snapshotSink_;
    std::uint64_t processedEvents_ = 0;
    std::unordered_map<int, LimitOrderBook> books_;
    std::unordered_map<std::string, int> orderToInstrument_;

    [[nodiscard]] int resolveInstrument(const MarketDataEvent& event) const;
    void updateOrderIndex(const MarketDataEvent& event, int instrumentId);
    void eraseInstrumentOrders(int instrumentId);
    void maybeSnapshot(const MarketDataEvent& event, int instrumentId);
};

} // namespace cmf

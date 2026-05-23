#include "order_book/BookDispatcher.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>

namespace cmf
{

AsyncSnapshotPrinter::AsyncSnapshotPrinter(std::ostream& output)
    : snapshots_{1024},
      output_{output},
      worker_{[this]() {
          std::string snapshot;
          while (snapshots_.pop(snapshot))
          {
              output_ << snapshot;
          }
      }}
{
}

AsyncSnapshotPrinter::~AsyncSnapshotPrinter()
{
    close();
}

void AsyncSnapshotPrinter::submit(std::string snapshot)
{
    if (!closed_)
    {
        snapshots_.push(std::move(snapshot));
    }
}

void AsyncSnapshotPrinter::close()
{
    if (closed_)
    {
        return;
    }

    closed_ = true;
    snapshots_.close();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

BookDispatcher::BookDispatcher(std::size_t snapshotEvery, SnapshotSink snapshotSink)
    : snapshotEvery_{snapshotEvery},
      snapshotSink_{std::move(snapshotSink)}
{
}

void BookDispatcher::process(const MarketDataEvent& event)
{
    const int instrumentId = resolveInstrument(event);
    if (instrumentId <= 0)
    {
        return;
    }

    LimitOrderBook& book = books_[instrumentId];
    book.apply(event);
    updateOrderIndex(event, instrumentId);

    ++processedEvents_;
    maybeSnapshot(event, instrumentId);
}

const LimitOrderBook* BookDispatcher::findBook(int instrumentId) const
{
    const auto book = books_.find(instrumentId);
    return book == books_.end() ? nullptr : &book->second;
}

void BookDispatcher::printFinalBest(std::ostream& output) const
{
    std::vector<int> instrumentIds;
    instrumentIds.reserve(books_.size());
    for (const auto& [instrumentId, book] : books_)
    {
        (void)book;
        instrumentIds.push_back(instrumentId);
    }
    std::sort(instrumentIds.begin(), instrumentIds.end());

    output << "Final best bid/ask:\n";
    for (const int instrumentId : instrumentIds)
    {
        const LimitOrderBook& book = books_.at(instrumentId);
        output << "Instrument " << instrumentId << ": ";

        const auto bid = book.bestBid();
        if (bid.has_value())
        {
            output << "bid " << std::fixed << std::setprecision(9) << LimitOrderBook::toDecimalPrice(bid->price)
                   << " x " << bid->quantity;
        }
        else
        {
            output << "bid null";
        }

        const auto ask = book.bestAsk();
        if (ask.has_value())
        {
            output << ", ask " << std::fixed << std::setprecision(9) << LimitOrderBook::toDecimalPrice(ask->price)
                   << " x " << ask->quantity;
        }
        else
        {
            output << ", ask null";
        }

        output << '\n';
    }
}

int BookDispatcher::resolveInstrument(const MarketDataEvent& event) const
{
    if (event.instrumentId > 0)
    {
        return event.instrumentId;
    }

    if (!event.orderId.empty())
    {
        const auto order = orderToInstrument_.find(event.orderId);
        if (order != orderToInstrument_.end())
        {
            return order->second;
        }
    }

    return 0;
}

void BookDispatcher::updateOrderIndex(const MarketDataEvent& event, int instrumentId)
{
    if (event.action == 'R')
    {
        eraseInstrumentOrders(instrumentId);
        return;
    }

    if (event.orderId.empty())
    {
        return;
    }

    const LimitOrderBook& book = books_.at(instrumentId);
    if (book.containsOrder(event.orderId))
    {
        orderToInstrument_[event.orderId] = instrumentId;
    }
    else if (event.action == 'C' || event.action == 'M')
    {
        orderToInstrument_.erase(event.orderId);
    }
}

void BookDispatcher::eraseInstrumentOrders(int instrumentId)
{
    for (auto it = orderToInstrument_.begin(); it != orderToInstrument_.end();)
    {
        if (it->second == instrumentId)
        {
            it = orderToInstrument_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void BookDispatcher::maybeSnapshot(const MarketDataEvent& event, int instrumentId)
{
    if (snapshotEvery_ == 0 || !snapshotSink_ || processedEvents_ % snapshotEvery_ != 0)
    {
        return;
    }

    const auto book = books_.find(instrumentId);
    if (book == books_.end())
    {
        return;
    }

    std::ostringstream snapshot;
    snapshot << "LOB snapshot after " << processedEvents_ << " events"
             << " at " << event.timestamp()
             << " for instrument " << instrumentId << ":\n";
    book->second.printSnapshot(snapshot);
    snapshot << '\n';
    snapshotSink_(snapshot.str());
}

} // namespace cmf

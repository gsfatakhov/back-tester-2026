#include "market_data/MarketDataPrinter.hpp"

#include <iostream>

namespace cmf
{

void processMarketDataEvent(const MarketDataEvent& event, std::ostream& output)
{
    output << "Timestamp: " << event.tsEvent.value_or("null")
           << ", Order ID: " << event.orderId
           << ", Instrument ID: " << event.instrumentId
           << ", Side: " << event.side
           << ", Price: ";

    if (event.price.has_value())
    {
        output << event.price.value();
    }
    else
    {
        output << "null";
    }

    output << ", Size: " << event.size
           << ", Action: " << event.action << '\n';
}

} // namespace cmf

#pragma once

#include "market_data/MarketDataEvent.hpp"

#include <iosfwd>

namespace cmf
{

void processMarketDataEvent(const MarketDataEvent& event, std::ostream& output);

} // namespace cmf

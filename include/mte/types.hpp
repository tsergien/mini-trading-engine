#pragma once

#include <cstdint>
#include <vector>

namespace mte {

// prices are integers in "ticks" (e.g. 1 tick = $0.0001, like ITCH).
using Price   = std::int64_t;
using Qty     = std::uint32_t;
using OrderID = std::uint64_t;

enum class Side : std::uint8_t { Bid, Ask };

struct OrderData
{
    Price   price;
    Qty     qty;
    Side    side;
};

struct OrderLevel
{
    Price price;
    Qty   total_qty;
    std::vector<OrderID> order_ids;  // in time priority order
};


struct Trade
{
    OrderID maker_id;  // resting order, already in the book
    OrderID taker_id;  // incoming order that crossed the spread
    Price   price;     // always the maker's price
    Qty     qty;
};

} // namespace mte

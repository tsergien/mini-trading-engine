#pragma once

#include "mte/types.hpp"

// #include <boost/container/unordered_flat_map.hpp>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>


namespace mte {
    
using h_table = std::unordered_map<OrderID, OrderData>;

class OrderBook
{
public:
    std::size_t add_limit(OrderID id, Side side, Price price, Qty qty,
                          std::vector<Trade>& out);

    // market order: matches at any price, never rests. Returns trades count.
    std::size_t add_market(OrderID id, Side side, Qty qty,
                           std::vector<Trade>& out);

    bool cancel(OrderID id);

    bool reduce(OrderID id, Qty new_qty);

    std::optional<Price> best_bid() const noexcept {return bids_.empty() ? std::nullopt : std::optional<Price>(bids_.front().price);};
    std::optional<Price> best_ask() const noexcept {return asks_.empty() ? std::nullopt : std::optional<Price>(asks_.front().price);};

    // total resting quantity at a price level (0 if level is empty)
    Qty volume_at(Side side, Price price) const;

    std::size_t order_count() const {return id_map_.size(); };

private:

    // most operations happen at highest prices
    std::vector<OrderLevel> bids_{}; // highest first
    //most operations happen at lowest price
    std::vector<OrderLevel> asks_{}; // from low to high 

    // rehashing but for now let's use large initial capacity
    h_table id_map_{};  // id -> (side, price)
};

} // namespace mte

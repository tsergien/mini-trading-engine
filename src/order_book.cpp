#include "mte/order_book.hpp"

namespace mte {

OrderBook::OrderBook()
{
    id_map_.reserve(1'000'000);
    bids_.reserve(1024);
    asks_.reserve(1024);
}

/**
 * asks: high -> low, (101, 100, 98, 96, 95)
 * bids: low -> high (88, 89, 90, 92, 94)
 * both sides are worst -> best, so the best level is at the back
 * (bids: first price >= 'price', asks: first price <= 'price'), or end().
 */
template<typename Levels>
auto binary_branchless(Levels &levels, const Side side, const Price price)
{
    const Price sign = (side == Side::Bid) ? 1 : -1;
    const Price key = price * sign;

    auto base = levels.begin();
    auto n = static_cast<std::ptrdiff_t>(levels.size());

    while (n > 1)
    {
        const std::ptrdiff_t half = n / 2;
        base += (base[half - 1].price * sign < key) ? half : 0;
        n -= half;
    }

    return base + (n == 1 && base->price * sign < key);
}


/**
 * Trade at this price level or 'better', if there's qty left - add to bids/asks. Return trades count.
 */
std::size_t OrderBook::add_limit(OrderID order_id, Side side,
    Price price, Qty qty, std::vector<Trade>& trades)
{
    assert(!id_map_.contains(order_id) && "Order id is already in the book");

    size_t trades_count{};
    auto &opposites = (side == Side::Bid) ? asks_ : bids_;
    auto &own = (side == Side::Bid) ? bids_ : asks_;

    auto cmp = [side, price](Price level_price) {
        return side == Side::Bid ? level_price <= price
                                : level_price >= price;
    };

    // sides are sorted worst -> best, so the best opposite level is always the last one
    Qty qty_to_trade = qty;
    while (!opposites.empty() && qty_to_trade > 0 && cmp(opposites.back().price))
    {
        auto &best_level = opposites.back();
        std::ptrdiff_t filled_orders = 0;

        for (const auto maker_id : best_level.order_ids)
        {
            auto maker_data_it = id_map_.find(maker_id);
            assert(maker_data_it != id_map_.end() && "Every id in a level must be in id_map_");
            auto &maker_data = maker_data_it->second;

            Qty cur_qty = std::min(qty_to_trade, maker_data.qty);

            trades.push_back({maker_id, order_id, best_level.price, cur_qty});
            trades_count++;

            qty_to_trade -= cur_qty;
            maker_data.qty -= cur_qty;
            best_level.total_qty -= cur_qty;

            if (maker_data.qty == 0)
            {
                id_map_.erase(maker_data_it);
                ++filled_orders;
            }

            if (qty_to_trade == 0)
                break;
        }

        best_level.order_ids.erase(best_level.order_ids.begin(),
            best_level.order_ids.begin() + filled_orders);

        // best level is the last one -> O(1) removal, nothing to shift
        if (best_level.total_qty == 0)
        {
            opposites.pop_back();
        }
    }

    if (qty_to_trade > 0)
    {
        auto level_it = binary_branchless(own, side, price);
        if (level_it == own.end() || level_it->price != price)
        {
            level_it = own.insert(level_it, {price, 0, {}});
        }

        level_it->total_qty += qty_to_trade;
        level_it->order_ids.push_back(order_id);

        id_map_.try_emplace(order_id, OrderData{price, qty_to_trade, side});
    }

    return trades_count;
}

std::size_t OrderBook::add_market(OrderID order_id, Side side, Qty qty, std::vector<Trade>& trades)
{
    assert(!id_map_.contains(order_id) && "Order id is already in the book");

    size_t trades_count{};
    auto &opposites = (side == Side::Bid) ? asks_ : bids_;

    Qty qty_to_trade = qty;
    while (!opposites.empty() && qty_to_trade > 0)
    {
        auto &cur_level = opposites.back();
        std::ptrdiff_t filled_orders = 0;

        for (const auto maker_id : cur_level.order_ids)
        {
            auto maker_data_it = id_map_.find(maker_id);
            assert(maker_data_it != id_map_.end() && "Every id in a level must be in id_map_");
            auto &maker_data = maker_data_it->second;

            Qty cur_qty = std::min(qty_to_trade, maker_data.qty);

            trades.push_back({maker_id, order_id, cur_level.price, cur_qty});
            trades_count++;

            qty_to_trade -= cur_qty;
            maker_data.qty -= cur_qty;
            cur_level.total_qty -= cur_qty;

            if (maker_data.qty == 0)
            {
                id_map_.erase(maker_data_it);
                ++filled_orders;
            }

            if (qty_to_trade == 0)
                break;
        }

        cur_level.order_ids.erase(cur_level.order_ids.begin(), cur_level.order_ids.begin() + filled_orders);

        // best level is the last one -> O(1) removal, nothing to shift
        if (cur_level.total_qty == 0)
        {
            opposites.pop_back();  // cur_level reference is dangling after this
        }
    }

    return trades_count;
}

bool OrderBook::cancel(OrderID order_id)
{
    auto it = id_map_.find(order_id);
    if (it == id_map_.end()) return false;

    // remove from the price level's order queue
    auto price = it->second.price;
    auto& levels = (it->second.side == Side::Bid) ? bids_ : asks_;
    auto level_it = binary_branchless(levels, it->second.side, price);

    assert(level_it != levels.end() && level_it->price == price && "Price level should exist when cancelling an order");

    auto& order_queue = level_it->order_ids;

    level_it->order_ids.erase(std::remove(order_queue.begin(), order_queue.end(), order_id), order_queue.end());

    level_it->total_qty -= it->second.qty;
    if (level_it->total_qty == 0)
    {
        levels.erase(level_it);
    }

    id_map_.erase(it);
    return true;
}

bool OrderBook::reduce(OrderID order_id, Qty new_qty)
{
    auto it = id_map_.find(order_id);

    if (it == id_map_.end() || it->second.qty <= new_qty)
        return false;

    if (new_qty == 0) return cancel(order_id);

    auto price = it->second.price;
    auto& levels = (it->second.side == Side::Bid) ? bids_ : asks_;
    auto level_it = binary_branchless(levels, it->second.side, price);

    assert(level_it != levels.end() && level_it->price == price && "Price level should exist when reducing an order");
    level_it->total_qty -= (it->second.qty - new_qty);

    it->second.qty = new_qty;
    
    return true;
}

Qty OrderBook::volume_at(Side side, Price price) const
{ 
    Qty total_qty = 0;
    const auto& levels = (side == Side::Bid) ? bids_ : asks_;

    auto it = binary_branchless(levels, side, price);
    while (it != levels.end() && it->price == price)
    {
        total_qty = it->total_qty;
        ++it;
    }

    return total_qty;
}


} // namespace mte

#include "mte/order_book.hpp"

namespace mte {


std::size_t OrderBook::add_limit(const OrderID order_id, const Side side,
    const Price price, const Qty qty, std::vector<Trade>& trades)
{
    size_t trades_count{};
    auto &opposites = (side == Side::Bid) ? asks_ : bids_;
    auto &own = (side == Side::Bid) ? bids_ : asks_;

    auto cmp = [side, price](Price level_price) {
        return side == Side::Bid ? level_price <= price
                                : level_price >= price;
    };

    auto best_level = std::find_if(opposites.begin(), opposites.end(),
        [cmp](const auto &opposite_level) { return cmp(opposite_level.price); });

    Qty qty_to_trade = qty;
    while (best_level != opposites.end() && cmp(best_level->price) && qty_to_trade > 0)
    {
        for (auto &maker_id : best_level->order_ids)
        {
            auto &maker_data = id_map_.at(maker_id);
            Qty cur_qty = std::min(qty_to_trade, maker_data.qty);

            trades.push_back({maker_id, order_id, best_level->price, cur_qty});
            trades_count++;

            qty_to_trade -= cur_qty;
            maker_data.qty -= cur_qty;
            best_level->total_qty -= cur_qty;

            if (maker_data.qty == 0)
            {
                id_map_.erase(maker_id);
                best_level->order_ids.erase(
                    std::remove(best_level->order_ids.begin(),
                    best_level->order_ids.end(), maker_id), best_level->order_ids.end()
                );
            }

            if (qty_to_trade == 0)
                break;
        }
    }
}

std::size_t OrderBook::add_market(OrderID order_id, Side side, Qty qty, std::vector<Trade>& out) {
    return 0;
}

bool OrderBook::cancel(OrderID order_id)
{
    auto it = id_map_.find(order_id);
    if (it == id_map_.end()) return false;

    // remove from the price level's order queue
    auto price = it->second.price;
    auto& levels = (it->second.side == Side::Bid) ? bids_ : asks_;
    auto level_it = std::find_if(levels.begin(), levels.end(), [price](const auto& level) { return level.price == price; });

    assert(level_it != levels.end() && "Price level should exist when cancelling an order");

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
    auto level_it = std::find_if(levels.begin(), levels.end(), [price](const auto& p) { return p.price == price; });

    assert(level_it != levels.cend() && "Price level should exist when reducing an order");
    level_it->total_qty -= (it->second.qty - new_qty);

    it->second.qty = new_qty;
    
    return true;
}

Qty OrderBook::volume_at(Side side, Price price) const
{ 
    Qty total_qty = 0;
    const auto& levels = (side == Side::Bid) ? bids_ : asks_;

    auto it = std::find_if(levels.begin(), levels.end(), [price](const auto& p) { return p.price == price; });
    while (it != levels.end() && it->price == price)
    {
        total_qty = it->total_qty;
        ++it;
    }

    return total_qty;
}


} // namespace mte

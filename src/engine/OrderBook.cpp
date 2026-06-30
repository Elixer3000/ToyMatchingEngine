#include "OrderBook.h"

#include <algorithm>

namespace {

bool isValidPrice(uint32_t price) {
    return price > 0 && price < 100;
}

bool isActiveState(OrderState state) {
    return state == OrderState::OPEN || state == OrderState::PARTIALLY_FILLED;
}

} // namespace

std::string sideToString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

std::string orderStateToString(OrderState state) {
    switch (state) {
        case OrderState::OPEN:
            return "OPEN";
        case OrderState::PARTIALLY_FILLED:
            return "PARTIALLY_FILLED";
        case OrderState::FILLED:
            return "FILLED";
        case OrderState::CANCELLED:
            return "CANCELLED";
    }
    return "UNKNOWN";
}

OrderBook::OrderBook(size_t pool_size)
    : best_bid_(0), best_ask_(100), next_trade_id_(1), pool_index_(0) {
    memory_pool_.resize(pool_size);
    all_orders_.reserve(pool_size);
    active_orders_.reserve(pool_size);
}

void OrderBook::reset() {
    for (auto& level : bids_) {
        level = PriceLevel();
    }
    for (auto& level : asks_) {
        level = PriceLevel();
    }
    best_bid_ = 0;
    best_ask_ = 100;
    next_trade_id_ = 1;
    pool_index_ = 0;
    all_orders_.clear();
    active_orders_.clear();
}

Order* OrderBook::allocateOrder() {
    if (pool_index_ >= memory_pool_.size()) {
        return nullptr;
    }
    return &memory_pool_[pool_index_++];
}

CommandResult OrderBook::processCommand(const Command& cmd) {
    if (cmd.type == CommandType::CANCEL) {
        return cancelOrder(cmd.order_id);
    }
    return submitLimitOrder(cmd.order_id, cmd.user_id, cmd.side, cmd.price, cmd.qty);
}

CommandResult OrderBook::submitLimitOrder(
    uint64_t order_id,
    uint64_t user_id,
    Side side,
    uint32_t price,
    uint32_t qty
) {
    CommandResult result{false, "REJECTED", "", order_id, 0, {}};

    if (order_id == 0 || user_id == 0) {
        result.reason = "order_id and user_id must be positive";
        return result;
    }
    if (!isValidPrice(price)) {
        result.reason = "price must be an integer from 1 to 99";
        return result;
    }
    if (qty == 0) {
        result.reason = "qty must be positive";
        return result;
    }
    if (all_orders_.find(order_id) != all_orders_.end()) {
        result.reason = "duplicate order_id";
        return result;
    }

    Order incoming{
        order_id,
        user_id,
        side,
        price,
        qty,
        qty,
        OrderState::OPEN,
        nullptr,
        nullptr
    };

    if (wouldSelfTrade(incoming)) {
        result.reason = "self-trade prevention rejected incoming order";
        return result;
    }

    Order* new_order = allocateOrder();
    if (!new_order) {
        result.reason = "order memory pool exhausted";
        return result;
    }

    *new_order = incoming;
    all_orders_[new_order->id] = new_order;

    matchOrder(new_order, result.trades);

    if (new_order->leaves_qty == 0) {
        new_order->state = OrderState::FILLED;
        result.status = "FILLED";
    } else {
        new_order->state = (new_order->leaves_qty == new_order->original_qty)
            ? OrderState::OPEN
            : OrderState::PARTIALLY_FILLED;
        addOrderToBook(new_order);
        active_orders_[new_order->id] = new_order;
        result.status = orderStateToString(new_order->state);
    }

    result.ok = true;
    result.reason = "accepted";
    result.leaves_qty = new_order->leaves_qty;
    return result;
}

CommandResult OrderBook::cancelOrder(uint64_t order_id) {
    CommandResult result{false, "REJECTED", "", order_id, 0, {}};

    auto known = all_orders_.find(order_id);
    if (known == all_orders_.end()) {
        result.status = "NOT_FOUND";
        result.reason = "unknown order_id";
        return result;
    }

    Order* order = known->second;
    if (!isActiveState(order->state)) {
        result.status = orderStateToString(order->state);
        result.reason = "order is not active";
        result.leaves_qty = order->leaves_qty;
        return result;
    }

    removeOrderFromBook(order);
    active_orders_.erase(order_id);
    order->state = OrderState::CANCELLED;

    result.ok = true;
    result.status = "CANCELLED";
    result.reason = "cancelled";
    result.leaves_qty = order->leaves_qty;
    return result;
}

bool OrderBook::wouldSelfTrade(const Order& incoming) const {
    if (incoming.side == Side::BUY) {
        for (uint32_t price = best_ask_; price < 100 && price <= incoming.price; ++price) {
            for (Order* order = asks_[price].head; order != nullptr; order = order->next) {
                if (order->user_id == incoming.user_id) {
                    return true;
                }
            }
        }
    } else {
        for (uint32_t price = best_bid_; price > 0 && price >= incoming.price; --price) {
            for (Order* order = bids_[price].head; order != nullptr; order = order->next) {
                if (order->user_id == incoming.user_id) {
                    return true;
                }
            }
            if (price == 1) {
                break;
            }
        }
    }
    return false;
}

void OrderBook::matchOrder(Order* incoming, std::vector<Trade>& trades) {
    while (incoming->leaves_qty > 0) {
        Order* resting = nullptr;

        if (incoming->side == Side::BUY) {
            if (best_ask_ < 100 && incoming->price >= best_ask_) {
                resting = asks_[best_ask_].head;
            } else {
                break;
            }
        } else {
            if (best_bid_ > 0 && incoming->price <= best_bid_) {
                resting = bids_[best_bid_].head;
            } else {
                break;
            }
        }

        if (!resting) {
            break;
        }

        const uint32_t match_qty = std::min(incoming->leaves_qty, resting->leaves_qty);
        trades.push_back(executeTrade(resting, incoming, match_qty));

        if (resting->leaves_qty == 0) {
            resting->state = OrderState::FILLED;
            removeOrderFromBook(resting);
            active_orders_.erase(resting->id);
        } else if (resting->leaves_qty < resting->original_qty) {
            resting->state = OrderState::PARTIALLY_FILLED;
        }
    }
}

Trade OrderBook::executeTrade(Order* resting, Order* incoming, uint32_t match_qty) {
    resting->leaves_qty -= match_qty;
    incoming->leaves_qty -= match_qty;

    PriceLevel& level = (resting->side == Side::BUY) ? bids_[resting->price] : asks_[resting->price];
    level.volume -= match_qty;

    const bool incoming_is_buy = incoming->side == Side::BUY;
    return Trade{
        next_trade_id_++,
        resting->id,
        incoming->id,
        incoming_is_buy ? incoming->user_id : resting->user_id,
        incoming_is_buy ? resting->user_id : incoming->user_id,
        resting->price,
        match_qty
    };
}

void OrderBook::addOrderToBook(Order* order) {
    PriceLevel& level = (order->side == Side::BUY) ? bids_[order->price] : asks_[order->price];

    order->prev = nullptr;
    order->next = nullptr;
    if (!level.head) {
        level.head = order;
        level.tail = order;
    } else {
        level.tail->next = order;
        order->prev = level.tail;
        level.tail = order;
    }

    level.volume += order->leaves_qty;

    if (order->side == Side::BUY && order->price > best_bid_) {
        best_bid_ = order->price;
    } else if (order->side == Side::SELL && order->price < best_ask_) {
        best_ask_ = order->price;
    }
}

void OrderBook::removeOrderFromBook(Order* order) {
    PriceLevel& level = (order->side == Side::BUY) ? bids_[order->price] : asks_[order->price];

    if (order->prev) {
        order->prev->next = order->next;
    } else {
        level.head = order->next;
    }

    if (order->next) {
        order->next->prev = order->prev;
    } else {
        level.tail = order->prev;
    }

    level.volume -= order->leaves_qty;
    order->prev = nullptr;
    order->next = nullptr;

    if (!level.head) {
        if (order->side == Side::BUY && order->price == best_bid_) {
            while (best_bid_ > 0 && bids_[best_bid_].head == nullptr) {
                --best_bid_;
            }
        } else if (order->side == Side::SELL && order->price == best_ask_) {
            while (best_ask_ < 100 && asks_[best_ask_].head == nullptr) {
                ++best_ask_;
            }
        }
    }
}

std::vector<OrderBook::SnapshotLevel> OrderBook::getBidsSnapshot() const {
    std::vector<SnapshotLevel> snapshot;
    for (int price = 99; price > 0; --price) {
        if (bids_[price].volume > 0) {
            snapshot.push_back({static_cast<uint32_t>(price), bids_[price].volume});
        }
    }
    return snapshot;
}

std::vector<OrderBook::SnapshotLevel> OrderBook::getAsksSnapshot() const {
    std::vector<SnapshotLevel> snapshot;
    for (int price = 1; price < 100; ++price) {
        if (asks_[price].volume > 0) {
            snapshot.push_back({static_cast<uint32_t>(price), asks_[price].volume});
        }
    }
    return snapshot;
}

std::optional<OrderView> OrderBook::getOrder(uint64_t order_id) const {
    auto it = all_orders_.find(order_id);
    if (it == all_orders_.end()) {
        return std::nullopt;
    }

    const Order* order = it->second;
    return OrderView{
        order->id,
        order->user_id,
        order->side,
        order->price,
        order->original_qty,
        order->leaves_qty,
        order->state
    };
}

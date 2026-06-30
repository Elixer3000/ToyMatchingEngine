#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class Side {
    BUY,
    SELL
};

enum class CommandType {
    SUBMIT,
    CANCEL
};

enum class OrderState {
    OPEN,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED
};

struct Command {
    CommandType type;
    uint64_t order_id;
    uint64_t user_id;
    Side side;
    uint32_t price;     // 1 to 99
    uint32_t qty;
};

struct Trade {
    uint64_t trade_id;
    uint64_t resting_order_id;
    uint64_t incoming_order_id;
    uint64_t buyer_user_id;
    uint64_t seller_user_id;
    uint32_t price;
    uint32_t qty;
};

struct CommandResult {
    bool ok;
    std::string status;
    std::string reason;
    uint64_t order_id;
    uint32_t leaves_qty;
    std::vector<Trade> trades;
};

struct OrderView {
    uint64_t id;
    uint64_t user_id;
    Side side;
    uint32_t price;
    uint32_t original_qty;
    uint32_t leaves_qty;
    OrderState state;
};

struct Order {
    uint64_t id;
    uint64_t user_id;
    Side side;
    uint32_t price;
    uint32_t original_qty;
    uint32_t leaves_qty;
    OrderState state;

    Order* prev;
    Order* next;
};

struct PriceLevel {
    Order* head;
    Order* tail;
    uint64_t volume;

    PriceLevel() : head(nullptr), tail(nullptr), volume(0) {}
};

class alignas(64) OrderBook {
private:
    PriceLevel bids_[100]; // indices 1-99
    PriceLevel asks_[100]; // indices 1-99

    uint32_t best_bid_;
    uint32_t best_ask_;
    uint64_t next_trade_id_;

    std::unordered_map<uint64_t, Order*> all_orders_;
    std::unordered_map<uint64_t, Order*> active_orders_;

    std::vector<Order> memory_pool_;
    size_t pool_index_;

    Order* allocateOrder();
    void addOrderToBook(Order* order);
    void removeOrderFromBook(Order* order);
    void matchOrder(Order* incoming, std::vector<Trade>& trades);
    Trade executeTrade(Order* resting, Order* incoming, uint32_t matched_qty);
    bool wouldSelfTrade(const Order& incoming) const;

public:
    explicit OrderBook(size_t pool_size = 1000000);

    void reset();
    CommandResult processCommand(const Command& cmd);
    CommandResult submitLimitOrder(uint64_t order_id, uint64_t user_id, Side side, uint32_t price, uint32_t qty);
    CommandResult cancelOrder(uint64_t order_id);

    struct SnapshotLevel {
        uint32_t price;
        uint64_t volume;
    };

    std::vector<SnapshotLevel> getBidsSnapshot() const;
    std::vector<SnapshotLevel> getAsksSnapshot() const;
    std::optional<OrderView> getOrder(uint64_t order_id) const;
};

std::string sideToString(Side side);
std::string orderStateToString(OrderState state);

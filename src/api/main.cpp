#include "crow_all.h"
#include "OrderBook.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace {

OrderBook order_book;
std::mutex engine_mutex;

crow::response jsonResponse(crow::json::wvalue body, int code = 200) {
    crow::response response;
    response.code = code;
    response.set_header("Content-Type", "application/json");
    response.write(body.dump());
    return response;
}

crow::json::wvalue errorResponseBody(const std::string& error) {
    crow::json::wvalue body;
    body["ok"] = false;
    body["error"] = error;
    return body;
}

bool getRequiredUint64(const crow::json::rvalue& body, const char* field, uint64_t& out, std::string& error) {
    if (!body.has(field)) {
        error = std::string("missing field: ") + field;
        return false;
    }

    const int64_t value = body[field].i();
    if (value <= 0) {
        error = std::string(field) + " must be positive";
        return false;
    }

    out = static_cast<uint64_t>(value);
    return true;
}

bool getRequiredUint32(const crow::json::rvalue& body, const char* field, uint32_t& out, std::string& error) {
    uint64_t value = 0;
    if (!getRequiredUint64(body, field, value, error)) {
        return false;
    }
    if (value > std::numeric_limits<uint32_t>::max()) {
        error = std::string(field) + " is too large";
        return false;
    }

    out = static_cast<uint32_t>(value);
    return true;
}

bool parseSide(const crow::json::rvalue& body, Side& side, std::string& error) {
    if (!body.has("side")) {
        error = "missing field: side";
        return false;
    }

    const std::string value = body["side"].s();
    if (value == "BUY") {
        side = Side::BUY;
        return true;
    }
    if (value == "SELL") {
        side = Side::SELL;
        return true;
    }

    error = "side must be BUY or SELL";
    return false;
}

crow::json::wvalue tradeToJson(const Trade& trade) {
    crow::json::wvalue item;
    item["trade_id"] = trade.trade_id;
    item["resting_order_id"] = trade.resting_order_id;
    item["incoming_order_id"] = trade.incoming_order_id;
    item["buyer_user_id"] = trade.buyer_user_id;
    item["seller_user_id"] = trade.seller_user_id;
    item["price"] = trade.price;
    item["qty"] = trade.qty;
    return item;
}

crow::json::wvalue resultToJson(const CommandResult& result) {
    crow::json::wvalue body;
    body["ok"] = result.ok;
    body["status"] = result.status;
    body["reason"] = result.reason;
    body["order_id"] = result.order_id;
    body["leaves_qty"] = result.leaves_qty;

    std::vector<crow::json::wvalue> trades;
    for (const auto& trade : result.trades) {
        trades.push_back(tradeToJson(trade));
    }
    body["trades"] = std::move(trades);
    return body;
}

int submitStatusCode(const CommandResult& result) {
    if (result.ok) {
        return 201;
    }
    if (result.reason == "order memory pool exhausted") {
        return 503;
    }
    if (result.reason == "duplicate order_id" ||
        result.reason == "self-trade prevention rejected incoming order") {
        return 409;
    }
    return 400;
}

crow::json::wvalue levelToJson(const OrderBook::SnapshotLevel& level) {
    crow::json::wvalue item;
    item["price"] = level.price;
    item["volume"] = level.volume;
    return item;
}

crow::json::wvalue orderToJson(const OrderView& order) {
    crow::json::wvalue body;
    body["order_id"] = order.id;
    body["user_id"] = order.user_id;
    body["side"] = sideToString(order.side);
    body["price"] = order.price;
    body["original_qty"] = order.original_qty;
    body["leaves_qty"] = order.leaves_qty;
    body["status"] = orderStateToString(order.state);
    return body;
}

} // namespace

int main() {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/health")
    ([]() {
        crow::json::wvalue body;
        body["ok"] = true;
        body["service"] = "binary-outcome-matching-engine";
        return jsonResponse(std::move(body));
    });

    CROW_ROUTE(app, "/order").methods(crow::HTTPMethod::Post)
    ([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return jsonResponse(errorResponseBody("invalid JSON"), 400);
        }

        uint64_t order_id = 0;
        uint64_t user_id = 0;
        uint32_t price = 0;
        uint32_t qty = 0;
        Side side = Side::BUY;
        std::string error;

        if (!getRequiredUint64(body, "order_id", order_id, error) ||
            !getRequiredUint64(body, "user_id", user_id, error) ||
            !parseSide(body, side, error) ||
            !getRequiredUint32(body, "price", price, error) ||
            !getRequiredUint32(body, "qty", qty, error)) {
            return jsonResponse(errorResponseBody(error), 400);
        }

        CommandResult result;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            result = order_book.submitLimitOrder(order_id, user_id, side, price, qty);
        }

        return jsonResponse(resultToJson(result), submitStatusCode(result));
    });

    CROW_ROUTE(app, "/order/<uint>").methods(crow::HTTPMethod::Delete)
    ([](uint64_t order_id) {
        CommandResult result;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            result = order_book.cancelOrder(order_id);
        }

        const int code = result.ok ? 200 : (result.status == "NOT_FOUND" ? 404 : 409);
        return jsonResponse(resultToJson(result), code);
    });

    CROW_ROUTE(app, "/order/<uint>")
    ([](uint64_t order_id) {
        std::optional<OrderView> order;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            order = order_book.getOrder(order_id);
        }

        if (!order) {
            return jsonResponse(errorResponseBody("unknown order_id"), 404);
        }
        return jsonResponse(orderToJson(*order));
    });

    CROW_ROUTE(app, "/orderbook")
    ([]() {
        std::vector<OrderBook::SnapshotLevel> bids;
        std::vector<OrderBook::SnapshotLevel> asks;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            bids = order_book.getBidsSnapshot();
            asks = order_book.getAsksSnapshot();
        }

        std::vector<crow::json::wvalue> json_bids;
        for (const auto& bid : bids) {
            json_bids.push_back(levelToJson(bid));
        }

        std::vector<crow::json::wvalue> json_asks;
        for (const auto& ask : asks) {
            json_asks.push_back(levelToJson(ask));
        }

        crow::json::wvalue body;
        body["bids"] = std::move(json_bids);
        body["asks"] = std::move(json_asks);
        return jsonResponse(std::move(body));
    });

    CROW_ROUTE(app, "/reset").methods(crow::HTTPMethod::Post)
    ([]() {
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            order_book.reset();
        }

        crow::json::wvalue body;
        body["ok"] = true;
        body["status"] = "RESET";
        return jsonResponse(std::move(body));
    });

    constexpr uint16_t port = 8080;
    std::cout << "[API] Starting REST server on port " << port << "\n";

    try {
        app.port(port).run();
    } catch (const std::exception& exc) {
        std::cerr << "[API] WARNING: Could not start server on port 8080. "
                  << "Port 8080 may already be in use. Stop the existing process and rerun "
                  << "./build/matching_engine. Details: " << exc.what() << "\n";
        return 1;
    }

    return 0;
}

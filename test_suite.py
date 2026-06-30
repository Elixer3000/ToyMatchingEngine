import argparse
import json
import sys
import time

import requests


BASE_URL = "http://localhost:8080"
TIMEOUT_SECONDS = 3
DEMO_LOGGER = None


def request(method, path, expected_status, **kwargs):
    response = requests.request(
        method,
        f"{BASE_URL}{path}",
        timeout=TIMEOUT_SECONDS,
        **kwargs,
    )
    if response.status_code != expected_status:
        raise AssertionError(
            f"{method} {path}: expected HTTP {expected_status}, "
            f"got {response.status_code}: {response.text}"
        )
    if response.text:
        return response.json()
    return {}


def ns_timestamp():
    return time.time_ns()


def format_ns(ts_ns):
    seconds = ts_ns // 1_000_000_000
    nanos = ts_ns % 1_000_000_000
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(seconds)) + f".{nanos:09d}"


def compact_json(value):
    return json.dumps(value, sort_keys=True)


def format_levels(levels):
    if not levels:
        return "(empty)"
    return " | ".join(f"{level['price']} x {level['volume']}" for level in levels)


class DemoLogger:
    def __init__(self, jsonl=False):
        self.jsonl = jsonl
        self.step = 0

    def emit(self, event_type, message, **fields):
        record = {
            "ts_ns": ns_timestamp(),
            "event": event_type,
            "message": message,
        }
        record.update(fields)
        if self.jsonl:
            print(json.dumps(record, sort_keys=True))
        else:
            self._print_pretty(record)

    def _print_pretty(self, record):
        event = record["event"]
        if event == "demo_start":
            print("\nBinary Outcome Matching Engine Emulator")
            print("=" * 46)
            print(f"Target: {BASE_URL}")
            print(f"Started: {format_ns(record['ts_ns'])} ({record['ts_ns']} ns)")
            print()
            return

        if event == "demo_end":
            print("\nDemo completed")
            print(f"Finished: {format_ns(record['ts_ns'])} ({record['ts_ns']} ns)")
            return

        if event == "scenario":
            print()
            print(record["message"])
            print("-" * len(record["message"]))
            return

        if event == "request":
            self.step += 1
            print(f"[{self.step:02d}] {record['message']}")
            print(f"     ts_ns: {record['ts_ns']} ({format_ns(record['ts_ns'])})")
            print(f"   request: {record['method']} {record['path']} expect {record['expected_status']}")
            if record.get("payload") is not None:
                print(f"   payload: {compact_json(record['payload'])}")
            return

        if event == "response":
            status = record["status_code"]
            elapsed = record["elapsed_ns"]
            body = record.get("body", {})
            print(f"  response: HTTP {status} in {elapsed} ns")
            self._print_body(body)
            print()

    def _print_body(self, body):
        if isinstance(body, dict) and "bids" in body and "asks" in body:
            print(f"      bids: {format_levels(body['bids'])}")
            print(f"      asks: {format_levels(body['asks'])}")
            return

        if isinstance(body, dict) and "trades" in body:
            print(
                "    result: "
                f"ok={body.get('ok')} status={body.get('status')} "
                f"order_id={body.get('order_id')} leaves={body.get('leaves_qty')} "
                f"reason={body.get('reason')}"
            )
            trades = body.get("trades") or []
            for trade in trades:
                print(
                    "     trade: "
                    f"id={trade['trade_id']} price={trade['price']} qty={trade['qty']} "
                    f"resting={trade['resting_order_id']} incoming={trade['incoming_order_id']} "
                    f"buyer={trade['buyer_user_id']} seller={trade['seller_user_id']}"
                )
            return

        if isinstance(body, dict) and "order_id" in body:
            print(
                "     order: "
                f"id={body.get('order_id')} user={body.get('user_id')} side={body.get('side')} "
                f"price={body.get('price')} original={body.get('original_qty')} "
                f"leaves={body.get('leaves_qty')} status={body.get('status')}"
            )
            return

        print(f"      body: {compact_json(body)}")


def log_event(event_type, message, **fields):
    DEMO_LOGGER.emit(event_type, message, **fields)


def demo_request(method, path, expected_status, description, **kwargs):
    started_ns = ns_timestamp()
    log_event(
        "request",
        description,
        method=method,
        path=path,
        expected_status=expected_status,
        payload=kwargs.get("json"),
    )

    response = requests.request(
        method,
        f"{BASE_URL}{path}",
        timeout=TIMEOUT_SECONDS,
        **kwargs,
    )
    elapsed_ns = ns_timestamp() - started_ns

    try:
        body = response.json() if response.text else {}
    except ValueError:
        body = response.text

    log_event(
        "response",
        description,
        method=method,
        path=path,
        status_code=response.status_code,
        elapsed_ns=elapsed_ns,
        body=body,
    )

    if response.status_code != expected_status:
        raise AssertionError(
            f"{method} {path}: expected HTTP {expected_status}, "
            f"got {response.status_code}: {response.text}"
        )
    return body


def reset():
    return request("POST", "/reset", 200)


def submit(order_id, user_id, side, price, qty, expected_status=201):
    return request(
        "POST",
        "/order",
        expected_status,
        json={
            "order_id": order_id,
            "user_id": user_id,
            "side": side,
            "price": price,
            "qty": qty,
        },
    )


def cancel(order_id, expected_status=200):
    return request("DELETE", f"/order/{order_id}", expected_status)


def book():
    return request("GET", "/orderbook", 200)


def order(order_id, expected_status=200):
    return request("GET", f"/order/{order_id}", expected_status)


def demo_reset():
    return demo_request("POST", "/reset", 200, "reset engine state")


def demo_submit(order_id, user_id, side, price, qty, expected_status=201, description=None):
    payload = {
        "order_id": order_id,
        "user_id": user_id,
        "side": side,
        "price": price,
        "qty": qty,
    }
    return demo_request(
        "POST",
        "/order",
        expected_status,
        description or f"submit {side} order {order_id}",
        json=payload,
    )


def demo_cancel(order_id, expected_status=200, description=None):
    return demo_request(
        "DELETE",
        f"/order/{order_id}",
        expected_status,
        description or f"cancel order {order_id}",
    )


def demo_book(description="snapshot order book"):
    return demo_request("GET", "/orderbook", 200, description)


def demo_order(order_id, expected_status=200, description=None):
    return demo_request(
        "GET",
        f"/order/{order_id}",
        expected_status,
        description or f"inspect order {order_id}",
    )


def assert_levels(actual, expected):
    compact = [(level["price"], level["volume"]) for level in actual]
    if compact != expected:
        raise AssertionError(f"expected levels {expected}, got {compact}")


def test_resting_orders_and_snapshot():
    reset()
    result = submit(1, 100, "BUY", 40, 10)
    assert result["ok"] is True
    assert result["status"] == "OPEN"
    assert result["leaves_qty"] == 10

    result = submit(2, 101, "SELL", 60, 5)
    assert result["status"] == "OPEN"

    snapshot = book()
    assert_levels(snapshot["bids"], [(40, 10)])
    assert_levels(snapshot["asks"], [(60, 5)])


def test_full_fill_uses_resting_price():
    reset()
    submit(10, 201, "SELL", 60, 10)

    result = submit(11, 202, "BUY", 65, 10)
    assert result["status"] == "FILLED"
    assert result["leaves_qty"] == 0
    assert len(result["trades"]) == 1
    assert result["trades"][0]["price"] == 60
    assert result["trades"][0]["qty"] == 10
    assert result["trades"][0]["resting_order_id"] == 10

    assert order(10)["status"] == "FILLED"
    assert order(11)["status"] == "FILLED"
    snapshot = book()
    assert_levels(snapshot["bids"], [])
    assert_levels(snapshot["asks"], [])


def test_partial_fill_accounting_and_cancel():
    reset()
    submit(20, 300, "BUY", 50, 100)

    result = submit(21, 301, "SELL", 45, 30)
    assert result["status"] == "FILLED"
    assert result["trades"][0]["price"] == 50
    assert result["trades"][0]["qty"] == 30

    resting = order(20)
    assert resting["status"] == "PARTIALLY_FILLED"
    assert resting["leaves_qty"] == 70
    assert_levels(book()["bids"], [(50, 70)])

    cancelled = cancel(20)
    assert cancelled["status"] == "CANCELLED"
    assert cancelled["leaves_qty"] == 70
    assert order(20)["status"] == "CANCELLED"
    assert_levels(book()["bids"], [])


def test_price_time_priority():
    reset()
    submit(30, 400, "SELL", 55, 5)
    submit(31, 401, "SELL", 55, 5)
    submit(32, 402, "SELL", 50, 5)

    result = submit(33, 403, "BUY", 60, 12)
    trades = result["trades"]
    assert [trade["resting_order_id"] for trade in trades] == [32, 30, 31]
    assert [trade["qty"] for trade in trades] == [5, 5, 2]
    assert order(31)["leaves_qty"] == 3
    assert_levels(book()["asks"], [(55, 3)])


def test_self_trade_rejects_incoming_atomically():
    reset()
    submit(40, 500, "BUY", 70, 10)

    result = submit(41, 500, "SELL", 60, 5, expected_status=409)
    assert result["ok"] is False
    assert result["status"] == "REJECTED"
    assert "self-trade" in result["reason"]
    assert_levels(book()["bids"], [(70, 10)])
    order(41, expected_status=404)


def test_validation_and_terminal_order_cancels():
    reset()
    submit(50, 600, "BUY", 40, 5)
    duplicate = submit(50, 601, "SELL", 40, 5, expected_status=409)
    assert duplicate["reason"] == "duplicate order_id"

    bad_side = submit(51, 601, "HOLD", 40, 5, expected_status=400)
    assert bad_side["ok"] is False

    bad_price = submit(52, 601, "BUY", 100, 5, expected_status=400)
    assert bad_price["reason"] == "price must be an integer from 1 to 99"

    bad_qty = submit(53, 601, "BUY", 40, 0, expected_status=400)
    assert bad_qty["error"] == "qty must be positive"

    cancel(999, expected_status=404)

    submit(54, 602, "SELL", 35, 5)
    assert order(50)["status"] == "FILLED"
    cancel_filled = cancel(50, expected_status=409)
    assert cancel_filled["reason"] == "order is not active"


def wait_for_server():
    for attempt in range(20):
        try:
            request("GET", "/health", 200)
            return
        except Exception:
            if attempt == 19:
                raise RuntimeError(
                    "server is not reachable on localhost:8080; start it with "
                    "./build/matching_engine in another terminal"
                )
            time.sleep(0.1)


def run_demo(jsonl=False):
    global DEMO_LOGGER
    DEMO_LOGGER = DemoLogger(jsonl=jsonl)

    wait_for_server()
    log_event("demo_start", "binary outcome matching engine scenario emulator")

    log_event("scenario", "Scenario 1: two-sided market with no cross")
    demo_reset()
    demo_submit(1001, 9001, "BUY", 42, 25, description="maker posts YES bid below ask")
    demo_submit(1002, 9002, "SELL", 60, 10, description="maker posts YES ask above bid")
    demo_book("book shows non-crossed liquidity on both sides")

    log_event("scenario", "Scenario 2: taker crosses ask and fills at resting price")
    demo_submit(1003, 9003, "BUY", 65, 7, description="buyer crosses best ask")
    demo_order(1002, description="resting ask is partially filled")
    demo_book("book after partial ask fill")

    log_event("scenario", "Scenario 3: large seller consumes multiple bid levels")
    demo_submit(1004, 9004, "BUY", 50, 10, description="second bid at better price")
    demo_submit(1005, 9005, "BUY", 48, 15, description="third bid behind best bid")
    demo_submit(1006, 9006, "SELL", 40, 22, description="seller crosses and walks the book")
    demo_order(1004, description="best bid was fully filled first")
    demo_order(1005, description="next bid was partially filled after price priority")
    demo_book("book after walking multiple bid levels")

    log_event("scenario", "Scenario 4: cancellation of remaining quantity")
    demo_cancel(1005, description="cancel remaining partially filled order")
    demo_book("book after cancel removes residual liquidity")

    log_event("scenario", "Scenario 5: self-trade prevention")
    demo_reset()
    demo_submit(2001, 9100, "BUY", 70, 20, description="user posts aggressive resting bid")
    demo_submit(
        2002,
        9100,
        "SELL",
        65,
        5,
        expected_status=409,
        description="same user attempts to sell into own bid",
    )
    demo_order(2002, expected_status=404, description="rejected self-trade order was not recorded")
    demo_book("resting self-trade-protected order remains unchanged")

    log_event("scenario", "Scenario 6: validation and terminal-state edge cases")
    demo_reset()
    demo_submit(3001, 9200, "BUY", 45, 5, description="valid order for duplicate test")
    demo_submit(
        3001,
        9201,
        "SELL",
        45,
        5,
        expected_status=409,
        description="duplicate order id is rejected",
    )
    demo_submit(
        3002,
        9201,
        "HOLD",
        45,
        5,
        expected_status=400,
        description="invalid side is rejected",
    )
    demo_submit(
        3003,
        9201,
        "BUY",
        100,
        5,
        expected_status=400,
        description="out-of-range price is rejected",
    )
    demo_cancel(999999, expected_status=404, description="unknown cancel returns not found")
    demo_submit(3004, 9202, "SELL", 40, 5, description="fills order 3001")
    demo_cancel(3001, expected_status=409, description="filled order cannot be cancelled")

    log_event("demo_end", "emulator completed")


def main():
    parser = argparse.ArgumentParser(description="Matching engine API tests and emulator")
    parser.add_argument(
        "--demo",
        action="store_true",
        help="run a timestamped scenario emulator instead of assertion-only tests",
    )
    parser.add_argument(
        "--jsonl",
        action="store_true",
        help="with --demo, emit raw JSON lines instead of the pretty walkthrough",
    )
    args = parser.parse_args()

    if args.demo:
        run_demo(jsonl=args.jsonl)
        return

    print(f"Running matching-engine API tests against {BASE_URL}")
    wait_for_server()

    tests = [
        test_resting_orders_and_snapshot,
        test_full_fill_uses_resting_price,
        test_partial_fill_accounting_and_cancel,
        test_price_time_priority,
        test_self_trade_rejects_incoming_atomically,
        test_validation_and_terminal_order_cancels,
    ]

    for test in tests:
        test()
        print(f"PASS {test.__name__}")

    print(f"PASS {len(tests)} test groups")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        sys.exit(1)

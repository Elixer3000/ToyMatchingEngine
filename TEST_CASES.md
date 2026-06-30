# Test Input Cases

These are the concrete API scenarios covered by `test_suite.py`. The same server can also be exercised as a timestamped emulator with:

```bash
python3 test_suite.py --demo
```

By default the emulator prints a readable walkthrough, if you intend to change it JSON, Use `python3 test_suite.py --demo --jsonl` to emit raw JSON lines. Both formats include each scenario, request, response, generated trade payload, and book snapshot with nanosecond timestamps.

## 1. Resting Orders And Snapshot

Input:

```json
{ "order_id": 1, "user_id": 100, "side": "BUY", "price": 40, "qty": 10 }
{ "order_id": 2, "user_id": 101, "side": "SELL", "price": 60, "qty": 5 }
```

Expected book:

```json
{
  "bids": [{ "price": 40, "volume": 10 }],
  "asks": [{ "price": 60, "volume": 5 }]
}
```

## 2. Full Fill At Resting Price

Input:

```json
{ "order_id": 10, "user_id": 201, "side": "SELL", "price": 60, "qty": 10 }
{ "order_id": 11, "user_id": 202, "side": "BUY", "price": 65, "qty": 10 }
```

Expected:

- order `11` fills completely
- order `10` fills completely
- trade price is `60`, the resting order price
- book is empty

## 3. Partial Fill Accounting And Cancel

Input:

```json
{ "order_id": 20, "user_id": 300, "side": "BUY", "price": 50, "qty": 100 }
{ "order_id": 21, "user_id": 301, "side": "SELL", "price": 45, "qty": 30 }
```

Expected:

- order `21` fills completely
- order `20` remains `PARTIALLY_FILLED` with `70` leaves quantity
- bid snapshot has `{ "price": 50, "volume": 70 }`
- `DELETE /order/20` cancels the remaining `70`

## 4. Price-Time Priority

Input:

```json
{ "order_id": 30, "user_id": 400, "side": "SELL", "price": 55, "qty": 5 }
{ "order_id": 31, "user_id": 401, "side": "SELL", "price": 55, "qty": 5 }
{ "order_id": 32, "user_id": 402, "side": "SELL", "price": 50, "qty": 5 }
{ "order_id": 33, "user_id": 403, "side": "BUY", "price": 60, "qty": 12 }
```

Expected trade order:

```text
32 first, because 50 is a better ask than 55
30 second, because it is first at price 55
31 third, partially filled for 2
```

Remaining book:

```json
{
  "asks": [{ "price": 55, "volume": 3 }]
}
```

## 5. Self-Trade Rejection

Input:

```json
{ "order_id": 40, "user_id": 500, "side": "BUY", "price": 70, "qty": 10 }
{ "order_id": 41, "user_id": 500, "side": "SELL", "price": 60, "qty": 5 }
```

Expected:

- second order returns `409`
- no trade occurs
- first order remains on the bid book with volume `10`
- rejected order `41` is not recorded

## 6. Validation And Terminal States

Covered cases:

- duplicate `order_id` returns `409`
- invalid `side` returns `400`
- invalid price outside `1..99` returns `400`
- zero quantity returns `400`
- unknown cancel returns `404`
- cancelling a filled order returns `409`

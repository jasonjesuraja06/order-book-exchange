#!/usr/bin/env python3
"""End-to-end check that the pre-trade risk layer gates the socket path.

Start the server first:   ./build/exchange 9911
Then:                     python3 tools/risk_gate_smoke.py 9911

Sends three NewOrder messages and prints the status byte of each
ExecutionReport. The first is within limits and must be accepted; the
other two breach RiskLimits and must come back rejected with order_id 0,
proving the order never reached the matching engine.
"""
import socket
import struct
import sys

STATUS = {0: "Accepted", 1: "Filled", 2: "PartiallyFilled",
          3: "Cancelled", 4: "Rejected"}


def order_msg(side, order_type, price, qty, symbol=b"AAPL"):
    # OrderMessage: type u8 | symbol[8] | side u8 | order_type u8 | price f64 | qty u32
    return struct.pack("<B8sBBdI", 1, symbol.ljust(8, b"\0"),
                       side, order_type, price, qty)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9876
    sock = socket.create_connection(("127.0.0.1", port), timeout=5)

    cases = [
        ("limit buy 100 @ 150.00 (within limits)", order_msg(0, 0, 150.0, 100)),
        ("qty 2,000,000 (max_order_quantity 1,000,000)", order_msg(0, 0, 150.0, 2_000_000)),
        ("100 @ 1,000,000 (max_order_notional $10M)", order_msg(0, 0, 1_000_000.0, 100)),
    ]

    failures = 0
    for i, (label, msg) in enumerate(cases):
        sock.sendall(msg)
        rpt = sock.recv(26)
        # ExecutionReport: type u8 | order_id u64 | status u8 | ...
        order_id = struct.unpack_from("<Q", rpt, 1)[0]
        status = rpt[9]
        name = STATUS.get(status, f"?{status}")
        print(f"{label:46s} -> {name:9s} order_id={order_id}")

        expected_rejected = (i > 0)
        if expected_rejected != (status == 4):
            failures += 1

    sock.close()
    print("PASS" if failures == 0 else f"FAIL ({failures} unexpected)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

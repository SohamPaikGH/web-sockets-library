from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosedOK
import asyncio
import logging
import pytest
import sys

logging.basicConfig(
    format="%(asctime)s %(message)s",
    level=logging.DEBUG
)

async def test_closing_handshake():
    message = "Hello World"
    async with connect("ws://localhost:1025") as ws:
        try:
            print("Closing connection...")
            await ws.close(code=1000, reason="Test complete.")
        except ConnectionClosedError:
            print("Connection abruptly severed or lost")
        except ConnectionClosedOK:
            print("Received 1000 (OK); then sent 1000 (OK)")

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

async def test_opening_handshake():
    async with connect("ws://localhost:1025") as ws:
        try:
            print("Opening handshake sent...")
            response = await ws.recv()
            print("Received: ", response, file=sys.stdout)
        except ConnectionClosedOK:
            print("Received 1000 (OK); then sent 1000 (OK)")
"""
def test_opening_handshake():
    asyncio.run(send_opening_handshake())
"""

import asyncio
import logging
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosedOK

logging.basicConfig(
    format="%(asctime)s %(message)s",
    level=logging.DEBUG
)

async def hello():
    async with connect("ws://localhost:1025", open_timeout=60) as ws:
        try:
            print("Opening handshake")
            response = await ws.recv()
            print("Response: ", response)
        except ConnectionClosedOK:
            print("Received 1000 (OK); then sent 1000 (OK)")
    print("Closing handshake")

asyncio.run(hello())

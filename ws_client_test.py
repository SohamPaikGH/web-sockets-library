import asyncio
import logging
from websockets.asyncio.client import connect

logging.basicConfig(
    format="%(asctime)s %(message)s",
    level=logging.DEBUG,
)

async def hello():
    async with connect("ws://localhost:1025") as ws:
        print("Opening handshake")
        response = await ws.recv()
        print(response)
        #pass
    print("Closing handshake")

asyncio.run(hello())

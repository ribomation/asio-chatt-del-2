#!/usr/bin/env python3
"""Quick & dirty klientsimulator: startar N klienter som ansluter, skickar
några rader med slumpmässiga mellanrum och kopplar ned. Bara standardbiblioteket.

    ./chat-sim.py --clients 200 --messages 20
"""
import argparse, asyncio, random, ssl


async def discard(reader):
    while await reader.read(4096):  # läs och släng inkommande broadcast
        pass


async def client(n, args):
    ctx = ssl.create_default_context(cafile=args.ca) if args.tls else None
    reader, writer = await asyncio.open_connection(args.host, args.port, ssl=ctx)
    sink = asyncio.create_task(discard(reader))
    for i in range(args.messages):
        writer.write(f"klient-{n}: meddelande {i}\n".encode())
        await writer.drain()
        await asyncio.sleep(random.uniform(args.min_delay, args.max_delay))
    writer.close()
    await writer.wait_closed()
    sink.cancel()


async def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="localhost")
    p.add_argument("--port", type=int, default=4711)
    p.add_argument("--clients", type=int, default=5)
    p.add_argument("--messages", type=int, default=20)
    p.add_argument("--min-delay", type=float, default=0.5, help="sekunder mellan rader (min)")
    p.add_argument("--max-delay", type=float, default=3.0, help="sekunder mellan rader (max)")
    p.add_argument("--tls", action="store_true")
    p.add_argument("--ca", help="serverns certifikat (självsignerat), med --tls")
    args = p.parse_args()
    await asyncio.gather(*(client(n, args) for n in range(args.clients)))


asyncio.run(main())

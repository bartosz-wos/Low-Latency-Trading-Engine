import asyncio
import websockets
import json
import csv
from datetime import datetime

async def data_sniper():
    url = "wss://stream.binance.com:9443/ws/btcusdt@depth5@100ms"
    filename = "binance_top5_data_v2.csv"

    print("--- Starting Binance WS Monitoring  ---")

    with open(filename, mode='a', newline='') as f:
        writer = csv.writer(f)

        async with websockets.connect(url) as ws:
            count = 0
            while True:
                try:
                    response = await ws.recv()
                    data = json.loads(response)

                    time = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")

                    bids = [it for lst in data['bids'] for it in lst]
                    asks = [it for lst in data['asks'] for it in lst]

                    row = [time] + bids + asks
                    writer.writerow(row)

                    count += 1

                    if count % 100 == 0:
                        print(f"[{time}] Got {count} samples...")

                except Exception as e:
                    print(f"Connection error: {e}")
                    break
if __name__ == "__main__":
    try:
        asyncio.run(data_sniper())
    except KeyboardInterrupt:
        print("\n--- Monitoring finished ---")

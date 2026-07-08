import asyncio
import aiohttp
import time

TARGET_URL = "http://localhost:8080/"
NUM_REQUESTS = 1000
CONCURRENT_LIMIT = 50  # Max simultaneous connections open at any split-second

async def fetch(session, request_id):
    try:
        start_time = time.time()
        async with session.get(TARGET_URL) as response:
            status = response.status
            # Read the body to force complete data streaming
            await response.text()
            latency = time.time() - start_time
            return {"id": request_id, "status": status, "latency": latency, "error": None}
    except Exception as e:
        return {"id": request_id, "status": None, "latency": 0, "error": str(e)}

async def main():
    # Limit maximum concurrency to avoid exhausting local OS ephemeral ports instantly
    semaphore = asyncio.Semaphore(CONCURRENT_LIMIT)
    
    async def wrapped_fetch(session, request_id):
        async with semaphore:
            return await fetch(session, request_id)

    print(f"Bombarding C Server on port 8080 with {NUM_REQUESTS} total requests...")
    start_total = time.time()
    
    async with aiohttp.ClientSession() as session:
        tasks = [wrapped_fetch(session, i) for i in range(NUM_REQUESTS)]
        results = await asyncio.gather(*tasks)
        
    total_time = time.time() - start_total
    
    # Process analytics
    successes = [r for r in results if r["status"] == 200]
    errors = [r for r in results if r["error"] is not None]
    latencies = [r["latency"] for r in successes]
    
    print("\n--- Stress Test Report ---")
    print(f"Total Completed Time: {total_time:.2f} seconds")
    print(f"Successful Requests (200 OK): {len(successes)} / {NUM_REQUESTS}")
    print(f"Failed / Errored Requests: {len(errors)} / {NUM_REQUESTS}")
    
    if latencies:
        avg_latency = sum(latencies) / len(latencies)
        print(f"Average Request Latency: {avg_latency * 1000:.2f} ms")
        print(f"Throughput: {len(successes) / total_time:.2f} requests/sec")
        
    if errors:
        print("\nSample Errors Encountered:")
        for err in errors[:5]:
            print(f" - {err['error']}")

if __name__ == "__main__":
    asyncio.run(main())
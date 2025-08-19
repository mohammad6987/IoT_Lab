import asyncio
import websockets
import json
import sqlite3
from datetime import datetime

# In-memory storage for valid cards (replace with database in production)
valid_cards = set()
# Or use a database:
# conn = sqlite3.connect('cards.db')
# c = conn.cursor()
# c.execute('''CREATE TABLE IF NOT EXISTS cards (uid TEXT PRIMARY KEY, added_date TEXT)''')
# conn.commit()

async def handle_card_client(websocket, path):
    print(f"Client connected from {websocket.remote_address}")
    
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                action = data.get('action')
                uid = data.get('uid')
                
                if not uid:
                    response = {"status": "error", "message": "No UID provided"}
                    await websocket.send(json.dumps(response))
                    continue
                
                if action == "check":
                    # Check if card is in database
                    if uid in valid_cards:
                    # Database version:
                    # c.execute("SELECT uid FROM cards WHERE uid=?", (uid,))
                    # if c.fetchone():
                        response = {"status": "authorized", "uid": uid}
                        print(f"Card {uid} authorized")
                    else:
                        response = {"status": "unauthorized", "uid": uid}
                        print(f"Card {uid} unauthorized")
                    
                    await websocket.send(json.dumps(response))
                
                elif action == "add":
                    # Add card to database
                    if uid not in valid_cards:
                        valid_cards.add(uid)
                        # Database version:
                        # c.execute("INSERT INTO cards VALUES (?, ?)", (uid, datetime.now().isoformat()))
                        # conn.commit()
                        response = {"status": "added", "uid": uid}
                        print(f"Card {uid} added to database")
                    else:
                        response = {"status": "exists", "uid": uid, "message": "Card already exists"}
                        print(f"Card {uid} already exists in database")
                    
                    await websocket.send(json.dumps(response))
                
                else:
                    response = {"status": "error", "message": "Unknown action"}
                    await websocket.send(json.dumps(response))
            
            except json.JSONDecodeError:
                response = {"status": "error", "message": "Invalid JSON"}
                await websocket.send(json.dumps(response))
    
    except websockets.exceptions.ConnectionClosed:
        print(f"Client {websocket.remote_address} disconnected")

async def main():
    server = await websockets.serve(handle_card_client, "0.0.0.0", 84)
    print("Card WebSocket server started on port 84")
    
    # For database version, add:
    # c.execute('''SELECT uid FROM cards''')
    # for row in c.fetchall():
    #     valid_cards.add(row[0])
    # print(f"Loaded {len(valid_cards)} cards from database")
    
    await server.wait_closed()

if __name__ == "__main__":
    asyncio.run(main())
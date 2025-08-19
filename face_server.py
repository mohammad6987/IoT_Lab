import asyncio
import websockets
import os
from deepface import DeepFace
from colorama import Fore, Style, init
import datetime
import json
import cv2
import numpy as np

init(autoreset=True)
COLOR_INFO = Fore.CYAN
COLOR_SUCCESS = Fore.GREEN
COLOR_FAILURE = Fore.RED
COLOR_WARNING = Fore.YELLOW
STYLE_BRIGHT = Style.BRIGHT

DB_PATH = "database"
TEMP_SAVE_DIR = "temp_photos"
os.makedirs(TEMP_SAVE_DIR, exist_ok=True)
os.makedirs(DB_PATH, exist_ok=True)

STATUS_CLIENTS = set()

def validate_face(image_path):
    """
    Validate if the image contains a human face
    Returns: (has_face, face_count, error_message)
    """
    try:
        # Try to detect faces in the image
        face_objs = DeepFace.extract_faces(
            img_path=image_path,
            detector_backend="opencv",
            enforce_detection=False,
            align=False
        )
        
        # Check if any faces were detected
        if not face_objs:
            return False, 0, "No faces detected in the image"
        
        # Count valid faces (with confidence above threshold)
        valid_faces = [face for face in face_objs if face.get('confidence', 0) > 0.8]
        face_count = len(valid_faces)
        
        if face_count == 0:
            return False, 0, "No clear human faces detected"
        elif face_count > 1:
            return True, face_count, f"Multiple faces detected ({face_count})"
        else:
            return True, face_count, "Single face detected"
            
    except Exception as e:
        return False, 0, f"Face validation error: {str(e)}"

def analyze_face(image_path):
    """
    Analyze face for recognition
    """
    try:
        print(f"{COLOR_INFO}[ANALYSIS] Starting face search for: {os.path.basename(image_path)}")
        
        # First validate the face
        has_face, face_count, message = validate_face(image_path)
        if not has_face:
            return {"status": "error", "message": message}
        
        # If multiple faces, warn but proceed
        if face_count > 1:
            print(f"{COLOR_WARNING}[WARNING] {message}")
        
        # Perform face recognition
        dfs = DeepFace.find(
            img_path=image_path,
            db_path=DB_PATH,
            model_name="VGG-Face",
            detector_backend="opencv",
            enforce_detection=False,
            silent=True
        )
        
        if dfs and not dfs[0].empty:
            best_match = dfs[0].iloc[0]
            identity = best_match['identity']
            person_name = os.path.basename(identity).split('.')[0]
            print(f"{STYLE_BRIGHT}{COLOR_SUCCESS}>> SUCCESS: Match Found! Identified as: {person_name}")
            return {"status": "granted", "person": person_name}
        else:
            print(f"{STYLE_BRIGHT}{COLOR_FAILURE}>> FAILURE: No match found in the database.")
            return {"status": "denied"}
            
    except Exception as e:
        print(f"{STYLE_BRIGHT}{COLOR_FAILURE}[ERROR] An unexpected error occurred during analysis: {e}")
        return {"status": "error", "message": str(e)}

async def delete_photo_after_delay(delay, path, filename):
    await asyncio.sleep(delay)
    try:
        os.remove(path)
        print(f"{COLOR_INFO}[SYSTEM] Cleaned up temporary file after {delay}s: {filename}")
    except Exception as e:
        print(f"{STYLE_BRIGHT}{COLOR_FAILURE}[SYSTEM] Error deleting file {filename}: {e}")

async def broadcast_status(message_dict):
    if STATUS_CLIENTS:
        message_json = json.dumps(message_dict)
        print(f"{COLOR_INFO}[BROADCAST] Sending status update to {len(STATUS_CLIENTS)} client(s): {message_json}")

        await asyncio.gather(
            *[client.send(message_json) for client in STATUS_CLIENTS],
            return_exceptions=True  
        )

async def photo_handler(websocket):
    source_ip = websocket.remote_address[0]
    print(f"\n[PHOTO_SERVER] ESP32 connected from: {source_ip}")
    try:
        async for message in websocket:
            if isinstance(message, str):
                # JSON header received
                header = json.loads(message)
                purpose = header.get("purpose", "check")
                expected_length = header.get("length", 0)
                print(f"[PHOTO_SERVER] Received header: purpose={purpose}, length={expected_length}")

            elif isinstance(message, bytes):
                # Image data received
                timestamp = datetime.datetime.now()
                filename = f"photo_{timestamp.strftime('%Y%m%d_%H%M%S_%f')}.jpg"
                image_path = os.path.join(TEMP_SAVE_DIR, filename)
                
                # Save the received image
                with open(image_path, "wb") as f:
                    f.write(message)
                print(f"[PHOTO_SERVER] Received and saved photo: {filename}")
                
                # Validate the image contains a face
                has_face, face_count, message = validate_face(image_path)
                
                if not has_face:
                    # No face detected, return error
                    response = {"status": "error", "message": message}
                    await websocket.send(json.dumps(response))
                    await broadcast_status({"status": "error", "message": message, "timestamp": timestamp.isoformat()})
                    
                    # Schedule deletion of invalid image
                    asyncio.create_task(delete_photo_after_delay(5, image_path, filename))
                    continue
                
                # Process based on purpose
                if purpose == "add":
                    # Validate it's a single face for registration
                    if face_count > 1:
                        response = {"status": "error", "message": "Multiple faces detected. Please provide an image with only one face for registration."}
                        await websocket.send(json.dumps(response))
                        await broadcast_status({"status": "error", "message": "Multiple faces in registration attempt", "timestamp": timestamp.isoformat()})
                        
                        # Schedule deletion
                        asyncio.create_task(delete_photo_after_delay(5, image_path, filename))
                        continue
                    
                    # Add the face to the database
                    dest_filename = f"user_{timestamp.strftime('%Y%m%d_%H%M%S')}.jpg"
                    dest_path = os.path.join(DB_PATH, dest_filename)
                    os.rename(image_path, dest_path)
                    
                    response = {"status": "added", "person": dest_filename.split('.')[0]}
                    print(f"[PHOTO_SERVER] Added new face to DB: {dest_filename}")
                    
                else:
                    # Authenticate face
                    analysis_result = await asyncio.to_thread(analyze_face, image_path)
                    response = {"status": analysis_result.get("status"),
                                "person": analysis_result.get("person", "N/A"),
                                "message": analysis_result.get("message", "")}
                
                await websocket.send(json.dumps(response))
                await broadcast_status({
                    "status": response["status"], 
                    "person": response.get("person", "N/A"), 
                    "timestamp": timestamp.isoformat(),
                    "message": response.get("message", "")
                })
                
                # Schedule deletion only if not added to DB
                if purpose != "add":
                    asyncio.create_task(delete_photo_after_delay(30, image_path, filename))

    except websockets.ConnectionClosed:
        print(f"[PHOTO_SERVER] ESP32 disconnected: {source_ip}")

async def status_handler(websocket):
    """
    Handle dashboard clients for receiving status updates
    """
    client_ip = websocket.remote_address[0]
    print(f"\n{COLOR_SUCCESS}[STATUS_SERVER] Dashboard client connected from: {client_ip}")
    STATUS_CLIENTS.add(websocket)
    try:
        # Send a welcome message
        welcome_msg = {
            "status": "connected", 
            "message": "Connected to face recognition status server",
            "timestamp": datetime.datetime.now().isoformat()
        }
        await websocket.send(json.dumps(welcome_msg))
        
        # Keep connection alive
        await websocket.wait_closed()
    finally:
        print(f"{COLOR_FAILURE}[STATUS_SERVER] Dashboard client disconnected: {client_ip}")
        STATUS_CLIENTS.remove(websocket)

async def main():
    if not os.path.exists(DB_PATH) or not os.listdir(DB_PATH):
        print(f"{STYLE_BRIGHT}{COLOR_WARNING}Warning: The 'database' folder is missing or empty.")
   
    print(f"{STYLE_BRIGHT}=============================================")
    print(f"  ENHANCED FACE RECOGNITION SERVER")
    print(f"  - Validates all images for human faces")
    print(f"  - Rejects images without clear faces")
    print(f"============================================={Style.RESET_ALL}")
   
    # Start both servers
    async with websockets.serve(photo_handler, "0.0.0.0", 80), \
               websockets.serve(status_handler, "0.0.0.0", 83):
        print(f"{COLOR_SUCCESS}[SYSTEM] Photo server running on ws://0.0.0.0:80")
        print(f"{COLOR_SUCCESS}[SYSTEM] Status broadcast server running on ws://0.0.0.0:83")
        print("Waiting for connections...")
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServers are shutting down.")
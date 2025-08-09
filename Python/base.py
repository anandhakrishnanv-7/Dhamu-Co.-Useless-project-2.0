import serial
import time
import pyttsx3
from playsound import playsound

# ---- SETTINGS ----
PORT = "COM5"        # Change to your Arduino's COM port
BAUD = 115200
FIXED_MP3 = r"D:\Useless Projects\Music\3.mp4"   # Audio for 'ready'
SCREAM_MP3 = r"D:\Useless Projects\Music\1.mp3"  # Aud`io for 'read'
  # Init TTS `engine
engine = pyttsx3.init()

# Serial connection
ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)
print(f"Connected to {PORT} at {BAUD} baud.\n")

try:
    while True:
        if ser.in_waiting > 0:
            raw_bytes = ser.readline()
            decoded = raw_bytes.decode(errors='ignore').strip().lower()
            print(f"Decoded: '{decoded}'")

            # --- READY: TTS + fixed audio ---
            if decoded == "ready":
                print("✅ Matched 'ready' from Arduino.")
                
                # Read next two lines for TTS
                line1 = ser.readline().decode(errors='ignore').strip()
                line2 = ser.readline().decode(errors='ignore').strip()
                tts_text = f"{line1} {line2}"
                print(f"TTS Output: {tts_text}")
                
                # Speak the text
                engine.say(tts_text)
                engine.runAndWait()
                
                # Play fixed audio
                print(f"🎵 Playing fixed audio: {FIXED_MP3}")
                playsound(FIXED_MP3)

            # --- READ: scream 3 times ---
            elif decoded == "read":
                print("😱 Matched 'read' from Arduino. Playing scream 3 times...")
                for i in range(3):
                    print(f"🔊 Scream #{i+1}")
                    playsound(SCREAM_MP3)

except KeyboardInterrupt:
    print("\nExiting...")
    ser.close()

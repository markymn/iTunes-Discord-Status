import time
import win32com.client
from pypresence import Presence

# Replace with your Discord Rich Presence App ID
CLIENT_ID = "USER_CLIENT_ID"

RPC = Presence(CLIENT_ID)
RPC.connect()

itunes = win32com.client.Dispatch("iTunes.Application")
current_song = None  # Track last played song to avoid redundant updates


def update_presence():
    """Updates Discord Rich Presence with current iTunes song"""
    try:
        track = itunes.CurrentTrack
        if track:
            song_name = track.Name
            artist_name = track.Artist
            RPC.update(
                details=f"{song_name}",
                state=f"by {artist_name}",
                large_image="itunes_logo",
                start=time.time()  # Shows elapsed time
            )
        else:
            RPC.clear()
    except Exception as e:
        print(f"Error updating presence: {e}")


# iTunes Event Listener to Detect Song Changes
class iTunesEvents:

    def onPlayerPlayEvent(self, track_id):
        update_presence()

    def onPlayerStopEvent(self, track_id):
        RPC.clear()


# Attach event listener
events = win32com.client.WithEvents(itunes, iTunesEvents)

print("iTunes Discord Presence Running...")

last_song = None  # Initialize last_song at the start

try:
    while True:
        try:
            track = itunes.CurrentTrack
            if track:
                song_name = track.Name
                artist_name = track.Artist

                if song_name != last_song:  # Only update if the song changed
                    RPC.update(
                        details=f"{song_name}",
                        state=f"by {artist_name}",
                        large_image="itunes_logo",
                        start=time.time()
                    )
                    last_song = song_name  # Store last song to prevent unnecessary updates

            else:
                RPC.clear()
                last_song = None

        except Exception as e:
            print(f"Error: {e}")

        time.sleep(1)  # Check for changes every second
except KeyboardInterrupt:
    print("\nExiting iTunes Discord Presence...")
    RPC.close()



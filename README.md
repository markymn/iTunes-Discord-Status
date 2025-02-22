# iTunes Discord Rich Presence

This script updates your Discord status with the song currently playing on iTunes using Discord Rich Presence.

Features

- Automatically updates Discord status with the current iTunes song.
- Displays song title, artist name, and elapsed time.
- Runs in the background and updates in real-time.
- Closes when iTunes is closed.

Installation & Setup

## 1. Install Python

  Ensure you have Python 3.12+ installed.

  - Download Python

  - Add Python to your system PATH during installation.

## 2. Install Required Python Packages

  Run the following command in a terminal or command prompt:

  > pip install pywin32 pypresence psutil

## 3. Enable Developer Mode in Discord

  Go to Discord Settings → Advanced → Enable Developer Mode

## 4. Create a Discord Rich Presence App

  1. Visit the Discord Developer Portal

  2. Click "New Application" and give it a name (e.g., iTunes Presence)

  3. Navigate to "Rich Presence" → "Assets"

  - Upload a large image (e.g., iTunes logo) and name it itunes_logo

  4. Copy the Application ID (you will need this for the script)

## 5. Edit the Script with Your App ID

  1. Open itunes_presence.py

  2. Find this line:

  > CLIENT_ID = "USER_CLIENT_ID"  # Replace with your App ID

  3. Replace it with your actual Application ID from the Developer Portal.

## 6. Run the Script

  Start the script by running:

  > python itunes_presence.py

  - It will launch iTunes and begin updating your Discord status.

  - To stop the script, simply close iTunes.

## Using the Batch and VBS Files for a Shortcut

  You have been provided with a .bat and a .vbs file to automate launching and closing the script. Here’s how to use them together:

  1. Ensure both files are in the same folder as itunes_presence.py.

  2. Run the start_itunes_presence.vbs file to launch everything seamlessly.

   - This will open iTunes, start the presence script in the background, and close the terminal window.

  3. To stop everything, simply close iTunes, and the script will shut down automatically.

## Creating a Shortcut for Easy Access

  1. Right-click the run_itunes.vbs file and select Create Shortcut.

  2. Move the shortcut to the Desktop or any preferred location.

  3. (Optional) Right-click the shortcut → Properties → Change Icon to select a custom icon.

  4. Drag the shortcut to the Taskbar for quick access.

## Troubleshooting

  ### Issue: python was not found

   - Ensure Python is added to your system PATH.

   - Try running: python --version

  ### Issue: iTunes opens but is frozen

   - Restart iTunes and run the script again.

   - Ensure iTunes is installed from the Apple website, not the Microsoft Store.

  ### Issue: Discord is not updating instantly

   - Discord takes a few seconds to update statuses.

   - Try restarting Discord and running the script again.

## Credits & License

This project is open-source under the MIT License. Feel free to contribute and improve it!

#!/bin/bash

# Setup script for Jukebox plugin music files
# This script creates the music directory and provides instructions for adding WAV files

echo "Setting up Jukebox plugin music directory..."

# Create the music directory
sudo mkdir -p /usr/share/millennium/music

# Set proper permissions
sudo chown -R $USER:$USER /usr/share/millennium/music
sudo chmod 755 /usr/share/millennium/music

echo "Created directory: /usr/share/millennium/music"

# Create a README with instructions
cat > /usr/share/millennium/music/README.md << 'EOF'
# Millennium Jukebox Music Files

This directory contains the audio files for the Jukebox plugin.

## Required Files

The following WAV files should be placed in this directory:

1. `bohemian_rhapsody.wav` - Bohemian Rhapsody by Queen
2. `hotel_california.wav` - Hotel California by Eagles  
3. `stairway_to_heaven.wav` - Stairway to Heaven by Led Zeppelin
4. `sweet_child_o_mine.wav` - Sweet Child O' Mine by Guns N' Roses
5. `imagine.wav` - Imagine by John Lennon
6. `billie_jean.wav` - Billie Jean by Michael Jackson
7. `like_a_rolling_stone.wav` - Like a Rolling Stone by Bob Dylan
8. `smells_like_teen_spirit.wav` - Smells Like Teen Spirit by Nirvana
9. `whats_going_on.wav` - What's Going On by Marvin Gaye

## Audio Format

The jukebox plugin uses **ALSA** (Advanced Linux Sound Architecture) via the `aplay` command:
- **WAV files only** - 44.1kHz, 16-bit, stereo recommended
- **aplay** - Standard ALSA command-line player
- **Direct hardware access** - No additional audio libraries needed

## Installation

ALSA is included by default on most Linux systems, including Raspberry Pi OS.
No additional installation is required for basic audio playback.

To verify ALSA is working:
```bash
# Test audio output
aplay /usr/share/sounds/alsa/Front_Left.wav

# List audio devices
aplay -l
```

## Converting MP3 to WAV

If you have MP3 files, convert them to WAV format:
```bash
# Install FFmpeg (if not already installed)
sudo apt-get install ffmpeg

# Convert MP3 to WAV (44.1kHz, 16-bit, stereo)
ffmpeg -i input.mp3 -ar 44100 -ac 2 -sample_fmt s16 output.wav
```

## Fallback Behavior

If no WAV files are found or aplay is not available,
the jukebox will fall back to playing simple beep tones.

## File Permissions

Make sure the audio files are readable by the daemon user:
```bash
sudo chmod 644 /usr/share/millennium/music/*.wav
```
EOF

echo "Created README with setup instructions"

# Create a simple test audio file (1 second of silence)
echo "Creating test audio file..."
ffmpeg -f lavfi -i anullsrc=duration=1 -ar 44100 -ac 2 -f wav /usr/share/millennium/music/test.wav 2>/dev/null || {
    echo "FFmpeg not available, creating simple test file..."
    # Create a simple test file that can be played with aplay
    echo "This is a test audio file for the Millennium Jukebox" > /usr/share/millennium/music/test.txt
}

echo ""
echo "Setup complete!"
echo ""
echo "Next steps:"
echo "1. Add your WAV files to /usr/share/millennium/music/"
echo "2. Test with: aplay /usr/share/millennium/music/test.wav"
echo "3. Convert MP3 files to WAV: ffmpeg -i song.mp3 -ar 44100 -ac 2 song.wav"
echo ""
echo "The jukebox plugin uses ALSA (aplay) for direct hardware audio playback."

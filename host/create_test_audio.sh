#!/bin/bash

# Create test audio files for the Millennium Jukebox
# This script generates simple test tones and melodies

echo "Creating test audio files for Millennium Jukebox..."

# Create the music directory
sudo mkdir -p /usr/share/millennium/music
sudo chown -R $USER:$USER /usr/share/millennium/music
sudo chmod 755 /usr/share/millennium/music

echo "Created directory: /usr/share/millennium/music"

# Check if FFmpeg is available
if ! command -v ffmpeg &> /dev/null; then
    echo "FFmpeg not found. Installing..."
    sudo apt-get update
    sudo apt-get install -y ffmpeg
fi

# Function to create a test tone
create_test_tone() {
    local filename="$1"
    local frequency="$2"
    local duration="$3"
    local name="$4"
    
    echo "Creating $name ($filename)..."
    ffmpeg -f lavfi -i "sine=frequency=$frequency:duration=$duration" \
           -ar 44100 -ac 2 -f wav \
           "/usr/share/millennium/music/$filename" \
           -y -loglevel quiet
    
    if [ $? -eq 0 ]; then
        echo "  ✅ Created $filename"
    else
        echo "  ❌ Failed to create $filename"
    fi
}

# Function to create a simple melody
create_melody() {
    local filename="$1"
    local name="$2"
    local notes="$3"
    
    echo "Creating $name ($filename)..."
    
    # Create a temporary file for the melody
    local temp_file="/tmp/melody_$filename"
    
    # Generate melody using multiple sine waves
    ffmpeg -f lavfi -i "sine=frequency=440:duration=0.5" \
           -f lavfi -i "sine=frequency=523:duration=0.5" \
           -f lavfi -i "sine=frequency=659:duration=0.5" \
           -f lavfi -i "sine=frequency=784:duration=1.0" \
           -filter_complex "[0][1][2][3]concat=n=4:v=0:a=1[out]" \
           -map "[out]" -ar 44100 -ac 2 -f wav \
           "/usr/share/millennium/music/$filename" \
           -y -loglevel quiet
    
    if [ $? -eq 0 ]; then
        echo "  ✅ Created $filename"
    else
        echo "  ❌ Failed to create $filename"
    fi
}

# Create test audio files
echo ""
echo "Generating test audio files..."

# Simple test tones
create_test_tone "test.wav" 440 2 "Test Tone (A4)"

# Create placeholder files for each song
create_melody "bohemian_rhapsody.wav" "Bohemian Rhapsody" "C-E-G-C"
create_melody "hotel_california.wav" "Hotel California" "B-D-G-B"
create_melody "stairway_to_heaven.wav" "Stairway to Heaven" "A-C-E-A"
create_melody "sweet_child_o_mine.wav" "Sweet Child O' Mine" "D-F-A-D"
create_melody "imagine.wav" "Imagine" "C-E-G-C"
create_melody "billie_jean.wav" "Billie Jean" "F-A-C-F"
create_melody "like_a_rolling_stone.wav" "Like a Rolling Stone" "G-B-D-G"
create_melody "smells_like_teen_spirit.wav" "Smells Like Teen Spirit" "E-G-B-E"
create_melody "whats_going_on.wav" "What's Going On" "A-C-E-A"

echo ""
echo "Test audio files created successfully!"
echo ""
echo "Files created in /usr/share/millennium/music/:"
ls -la /usr/share/millennium/music/

echo ""
echo "To test the jukebox:"
echo "1. Run the daemon: ./daemon"
echo "2. Activate Jukebox plugin via web portal"
echo "3. Insert 25¢ and select a song (1-9)"
echo "4. You should hear the test melodies!"

echo ""
echo "To replace with real music:"
echo "1. Convert your MP3 files to WAV:"
echo "   ffmpeg -i song.mp3 -ar 44100 -ac 2 song.wav"
echo "2. Copy to /usr/share/millennium/music/"
echo "3. Make sure filenames match the expected names"


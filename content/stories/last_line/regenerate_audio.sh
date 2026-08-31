#!/usr/bin/env bash
set -euo pipefail

command -v say >/dev/null || { echo "macOS say is required" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg is required" >&2; exit 1; }
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MEDIA="$SCRIPT_DIR/media"
mkdir -p "$MEDIA"

while IFS='|' read -r name words; do
    [ -n "$name" ] || continue
    temporary="$(mktemp "${TMPDIR:-/tmp}/last-line-audio.XXXXXX.aiff")"
    say -v Samantha -r 155 -o "$temporary" "$words"
    ffmpeg -hide_banner -loglevel error -y -i "$temporary" \
        -af 'loudnorm=I=-20:TP=-2:LRA=7' -ar 8000 -ac 1 -c:a pcm_s16le \
        "$MEDIA/$name.wav"
    rm -f "$temporary"
done <<'TRANSCRIPTS'
incoming_line|A line is ringing. Lift the receiver to answer.
choose_message|This is the last line. Save one message. Press one for the past, or two for the future. Insert a coin or present a token if you are curious. Press star to repeat.
repeat_choice|Press one for the past, or two for the future. Press star to hear this again.
coin_secret|The coin remembers every hand it crossed. Press pound to return to the line.
token_secret|Your token opens a line that was not on the map. Press pound to return.
past_selected|The past has been selected. Please hold while the last line connects.
future_selected|The future has been selected. Please hold while the last line connects.
connecting_line|Connecting the last line. If no one answers, your message will still be kept.
past_delivered|Your message reached the past. Return tomorrow. The line will remember.
future_sealed|Your message is sealed for the future. Return tomorrow. The line will remember.
call_saved|Your call is saved. Lift the receiver when you are ready to resume.
come_back|The line remembers. Come back soon.
welcome_back|You came back. The line remembers what you chose.
past_coda|The past answered. Your words remain where you left them.
future_coda|The future heard you. Your words remain where you sent them.
veteran_line|The line knows you now. Press one for the past, or two for the future. Secrets still listen for coins and tokens.
TRANSCRIPTS

echo "Generated $(find "$MEDIA" -type f -name '*.wav' | wc -l | tr -d ' ') clips in $MEDIA"

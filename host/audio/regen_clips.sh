#!/usr/bin/env bash
#
# regen_clips.sh — rebuild "The Operator" voice clips from clips.manifest.
#
# For each clip in the manifest it calls ElevenLabs text-to-speech, converts the
# result to the daemon's clip format (8 kHz mono 16-bit PCM WAV), writes it to
# OUTDIR, and optionally deploys to the Pi. The recorded WAVs are not in git, so
# this is how you restore them after an SD reflash or change the script.
#
# Requires: curl, ffmpeg, python3, and an ElevenLabs API key with TTS access.
#
# Usage:
#   ELEVENLABS_API_KEY=sk_... ./regen_clips.sh            # build into ./out
#   ELEVENLABS_API_KEY=sk_... ./regen_clips.sh --deploy   # build + copy to Pi
#
# Env knobs (all optional):
#   ELEVENLABS_API_KEY  required  — API key (never hard-code it / commit it)
#   VOICE_ID            default Sarah (EXAVITQu4vr4xnSDxMaL)
#   MODEL_ID            default eleven_multilingual_v2
#   OUTDIR              default <script dir>/out
#   PI_HOST             default matzen@192.168.86.152   (for --deploy)
#   SSH_KEY             default ~/.ssh/id_ed25519_sk_anima_notouch
#   CLIP_DIR            default /usr/local/share/millennium/audio  (on the Pi)
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
MANIFEST="$HERE/clips.manifest"
OUTDIR="${OUTDIR:-$HERE/out}"
VOICE_ID="${VOICE_ID:-EXAVITQu4vr4xnSDxMaL}"   # Sarah
MODEL_ID="${MODEL_ID:-eleven_multilingual_v2}"
PI_HOST="${PI_HOST:-matzen@192.168.86.152}"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_ed25519_sk_anima_notouch}"
CLIP_DIR="${CLIP_DIR:-/usr/local/share/millennium/audio}"
DEPLOY=0
[ "${1:-}" = "--deploy" ] && DEPLOY=1

die() { echo "error: $*" >&2; exit 1; }
command -v curl    >/dev/null || die "curl not found"
command -v ffmpeg  >/dev/null || die "ffmpeg not found"
command -v python3 >/dev/null || die "python3 not found"
[ -f "$MANIFEST" ] || die "manifest not found: $MANIFEST"
[ -n "${ELEVENLABS_API_KEY:-}" ] || die "set ELEVENLABS_API_KEY (TTS-capable key)"

mkdir -p "$OUTDIR"
echo "Voice $VOICE_ID · model $MODEL_ID · out $OUTDIR"

built=0
while read -r name text || [ -n "$name" ]; do
    # skip blanks / comments (read leaves name empty on a blank line)
    [ -z "${name:-}" ] && continue
    case "$name" in \#*) continue ;; esac
    [ -n "${text:-}" ] || { echo "  skip $name (no text)"; continue; }

    body="$(python3 -c 'import json,sys; print(json.dumps({"text":sys.argv[1],"model_id":sys.argv[2]}))' "$text" "$MODEL_ID")"
    mp3="$OUTDIR/$name.mp3"; wav="$OUTDIR/$name.wav"

    code="$(curl -s -w '%{http_code}' -X POST \
        "https://api.elevenlabs.io/v1/text-to-speech/$VOICE_ID" \
        -H "xi-api-key: $ELEVENLABS_API_KEY" -H "Content-Type: application/json" \
        -d "$body" --output "$mp3")"

    if [ "$code" != "200" ] || head -c1 "$mp3" | grep -qa '{'; then
        echo "  FAIL $name: HTTP $code -> $(head -c 200 "$mp3" 2>/dev/null)"
        rm -f "$mp3"; die "TTS failed for '$name' (check key/voice/plan)"
    fi

    ffmpeg -y -loglevel error -i "$mp3" -ac 1 -ar 8000 -sample_fmt s16 \
        -c:a pcm_s16le "$wav"
    rm -f "$mp3"
    [ "$(head -c4 "$wav")" = "RIFF" ] || die "bad WAV produced for '$name'"
    printf '  ok   %-11s %s bytes\n' "$name" "$(wc -c <"$wav" | tr -d ' ')"
    built=$((built+1))
done < "$MANIFEST"

echo "built $built clip(s) into $OUTDIR"

if [ "$DEPLOY" = "1" ]; then
    [ -f "$SSH_KEY" ] || die "ssh key not found: $SSH_KEY"
    SSH="ssh -i $SSH_KEY -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=8"
    echo "deploying to $PI_HOST:$CLIP_DIR"
    $SSH "$PI_HOST" 'rm -rf ~/op_clips_in && mkdir -p ~/op_clips_in'
    scp -i "$SSH_KEY" -o IdentitiesOnly=yes -o BatchMode=yes "$OUTDIR"/*.wav "$PI_HOST":op_clips_in/ >/dev/null
    $SSH "$PI_HOST" "sudo mkdir -p $CLIP_DIR && sudo cp ~/op_clips_in/*.wav $CLIP_DIR/ && sudo chmod 644 $CLIP_DIR/*.wav && rm -rf ~/op_clips_in && ls -1 $CLIP_DIR"
    echo "deployed."
fi

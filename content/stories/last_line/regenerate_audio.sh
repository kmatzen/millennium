#!/usr/bin/env bash
set -euo pipefail

command -v curl >/dev/null || { echo "curl is required" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg is required" >&2; exit 1; }
command -v jq >/dev/null || { echo "jq is required" >&2; exit 1; }
: "${ELEVENLABS_API_KEY:?Set ELEVENLABS_API_KEY in the environment}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MEDIA="$SCRIPT_DIR/media"
BUILD="${TMPDIR:-/tmp}/last-line-elevenlabs"
mkdir -p "$MEDIA" "$BUILD/voice" "$BUILD/sfx"

# Stock voice IDs may be overridden without editing the production transcript.
OPERATOR_VOICE="${OPERATOR_VOICE:-onwK4e9ZLuTAKqWW03F9}" # Daniel
MARA_VOICE="${MARA_VOICE:-cgSgspJ2msm6clMCkdW9}" # Jessica
ADULT_MARA_VOICE="${ADULT_MARA_VOICE:-XrExE9yKIg1WjnnlVkGX}" # Matilda
FATHER_VOICE="${FATHER_VOICE:-nPczCjzI2devNBz1zQrb}" # Brian
LINE_VOICE="${LINE_VOICE:-cgSgspJ2msm6clMCkdW9}" # Mara, processed
TTS_MODEL="${TTS_MODEL:-eleven_multilingual_v2}"

tts() {
  local name="$1" voice="$2" words="$3" out="$BUILD/voice/$1.mp3" body
  body="$(jq -n --arg text "$words" --arg model "$TTS_MODEL" '{text:$text,model_id:$model,voice_settings:{stability:0.48,similarity_boost:0.76,style:0.28,use_speaker_boost:true}}')"
  curl --fail-with-body --silent --show-error -o "$out" -X POST \
    "https://api.elevenlabs.io/v1/text-to-speech/$voice?output_format=mp3_44100_128" \
    -H "xi-api-key: $ELEVENLABS_API_KEY" -H 'Content-Type: application/json' --data-binary "$body"
}

sfx() {
  local name="$1" seconds="$2" prompt="$3" out="$BUILD/sfx/$1.mp3" body
  [ -s "$out" ] && return
  body="$(jq -n --arg text "$prompt" --argjson duration "$seconds" '{text:$text,duration_seconds:$duration,prompt_influence:0.55,model_id:"eleven_text_to_sound_v2"}')"
  curl --fail-with-body --silent --show-error -o "$out" -X POST \
    'https://api.elevenlabs.io/v1/sound-generation?output_format=mp3_44100_128' \
    -H "xi-api-key: $ELEVENLABS_API_KEY" -H 'Content-Type: application/json' --data-binary "$body"
}

finish() {
  local name="$1" bed="${2:-}" voice="$BUILD/voice/$1.mp3" voice_filter='volume=1.0'
  if [ "$name" = "answer_coda" ] || [ "$name" = "answer_result" ]; then
    voice_filter='asetrate=44100*0.985,aresample=44100,aecho=0.8:0.35:45:0.10'
  fi
  if [ -n "$bed" ]; then
    ffmpeg -nostdin -hide_banner -loglevel error -y -i "$voice" -stream_loop -1 -i "$BUILD/sfx/$bed.mp3" \
      -filter_complex "[0:a]$voice_filter[v];[1:a]volume=0.12[b];[v][b]amix=inputs=2:duration=first:dropout_transition=0[m];[m]highpass=f=220,lowpass=f=3500,loudnorm=I=-20:TP=-2:LRA=7[out]" \
      -map '[out]' -ar 8000 -ac 1 -c:a pcm_s16le "$MEDIA/$name.wav"
  else
    ffmpeg -nostdin -hide_banner -loglevel error -y -i "$voice" \
      -af "$voice_filter,highpass=f=180,lowpass=f=3800,loudnorm=I=-20:TP=-2:LRA=7" \
      -ar 8000 -ac 1 -c:a pcm_s16le "$MEDIA/$name.wav"
  fi
}

sfx ring 5 'An old American landline telephone rings twice in a silent dark hallway, realistic mechanical bells, ominous but subtle, dry close recording, no music, no voices'
sfx line 12 'Quiet vintage telephone line ambience, faint electrical hiss and intermittent distant switching clicks, unsettling but subtle, seamless, no ringing, no music, no voices'
sfx rain 12 'Steady night rain heard from inside an old wooden house, a distant clock and one faint floorboard creak, restrained cinematic realism, no music, no voices'
sfx fire 12 'Subtle house fire beginning in another room, faint crackling wood with distant rain outside, tense but not loud, no music, no voices'

while IFS='|' read -r name role bed words; do
  [ -n "$name" ] || continue
  case "$role" in
    operator) voice="$OPERATOR_VOICE" ;;
    mara) voice="$MARA_VOICE" ;;
    adult_mara) voice="$ADULT_MARA_VOICE" ;;
    father) voice="$FATHER_VOICE" ;;
    line) voice="$LINE_VOICE" ;;
    *) echo "Unknown role: $role" >&2; exit 1 ;;
  esac
  printf 'Generating %-22s (%s)\n' "$name" "$role"
  tts "$name" "$voice" "$words"
  finish "$name" "$bed"
done <<'TRANSCRIPTS'
invitation|line|ring|This call is for you. Lift the receiver to answer.
missed|line|line|Missed call. It will ring again when the line is ready.
operator_intro|operator|line|Operator seventeen. I have a child waiting on a disconnected line. She asked for this number. Press pound to accept the call, or star to repeat.
operator_repeat|operator|line|A child is waiting on a line that should not exist. Press pound to answer, or star to hear the message again.
mara_intro|mara|rain|My name is Mara. I am nine. My father is not home, and someone upstairs is calling me by the name only he knows. Press one to stay with me, or two to ask the Operator to trace the call.
mara_repeat|mara|rain|Please do not go. There is a voice upstairs. Press one to stay on the line with me, or two to trace where I am calling from.
stay_with_mara|line|rain|Do not hang up. Mara has put down the receiver. Through the line you hear rain, a clock striking eleven seventeen, and slow footsteps crossing the room above her.
voice_clue|mara|rain|The upstairs voice says, Little Wren, come find me. Mara whispers that Little Wren is the secret name her father used when nobody else was listening.
trace_call|operator|line|Tracing the line. The exchange is gone. The street was renamed. The date on the circuit is October seventeenth, nineteen eighty nine.
operator_report|operator|line|Operator report. The house burned before midnight on October seventeenth, nineteen eighty nine. One adult body was recovered. No child was found. No telephone call was logged.
upstairs_calls|mara|fire|The voice upstairs calls again. Mara, the smoke is getting thick. I know you are on the telephone. Ask them what I should do.
coin_secret|father|line|Little Wren, if the voice upstairs sounds like me, do not answer it. Take your coat and leave through the kitchen door. Press pound to return.
token_secret|operator|line|The token opens a property record dated twenty nineteen. The empty lot was purchased by Mara Vale, age thirty nine. The permit describes one structure: a public telephone with no connected service. Press pound to return.
decision|operator|line|Only your next words can cross the line. Press one to tell Mara to leave the house now. Press two to tell her to answer the voice upstairs. Press star to hear the choice again. A coin or token may reveal more.
decision_repeat|operator|line|What should Mara do? Press one: leave now. Press two: answer the voice upstairs. Press star for the full message.
send_leave|line|line|Your message is: Leave now. Do not look back. Connecting your voice to nineteen eighty nine.
send_answer|line|line|Your message is: Answer it, but do not say your name. Connecting your voice to nineteen eighty nine.
bridge|operator|line|The line is open. Do not speak. What crosses next does not belong to our year.
leave_result|mara|rain|Mara repeats your words. A kitchen door slams. Small footsteps run into the rain. Upstairs, the other voice keeps calling to an empty house. Return when the telephone rings again.
answer_result|line|fire|Mara climbs the stairs. A receiver clicks in the room above. Two voices say hello at the same time. Both voices belong to Mara, but one has been waiting thirty years. Return when the telephone rings again.
interrupted|operator|line|The line is holding your place. Lift the receiver to continue. If you leave it too long, the call will wait until another night.
held_for_return|operator|line|The call has been held overnight. The line remembers where you left it.
return_leave|adult_mara|line|Message for the person who answered me in nineteen eighty nine. My name is Mara Vale. I lived. Press pound, or wait, to hear the rest.
leave_coda|adult_mara|rain|I spent thirty years looking for the number that saved me. There was no number, so I built this telephone and waited for you to answer it. The loop is closed. Thank you.
return_answer|operator|line|The upstairs line is ringing again. This time, it is calling from inside the telephone. Press pound, or wait, to listen.
answer_coda|line|line|Hello? I remember the voice that told me to answer. Now the other Mara remembers it too. She knows your voice. Whatever happens, do not tell her your name.
TRANSCRIPTS

echo "Generated $(find "$MEDIA" -type f -name '*.wav' | wc -l | tr -d ' ') production clips in $MEDIA"

#!/usr/bin/env bash
# Save Boo Pumpkin camera frames into one folder per type of shot.
#
#   capture <type>          save frames into training/<type>/ until Ctrl+C
#   capture list            show every type with its frame count
#   capture watch           live person score; says BOO and vibrates (Termux) when it triggers
#   capture quiet on|off    silence detection boos (/boo on the status page still plays)
#   capture vol <0-30>      set the speaker volume
#   capture find            look for the pumpkin on this network and remember its address
#
# A new type asks before creating its folder, so a typo does not start a new one.
# The page password is asked for once and kept in ~/.boo-pumpkin (only readable by you).
# Address: PUMPKIN=<ip> if set, else the one `capture find` saved, else boo-pumpkin.local.
# Frames go to boo-pumpkin/frames/ (git-ignored), or to shared storage in Termux. FRAMES_DIR overrides both.
set -euo pipefail

config="$HOME/.boo-pumpkin"
saved_ip=""
password="${PUMPKIN_PASSWORD:-}"
if [[ -f "$config" ]]; then
  saved_ip=$(sed -n 's/^ip=//p' "$config")
  [[ -n "$password" ]] || password=$(sed -n 's/^password=//p' "$config")
fi
pumpkin="${PUMPKIN:-${saved_ip:-boo-pumpkin.local}}"
gap="${GAP:-0.3}"
if [[ "${PREFIX:-}" == *com.termux* ]]; then
  frames="${FRAMES_DIR:-$HOME/storage/shared/pumpkin-frames}"
else
  frames="${FRAMES_DIR:-$(cd "$(dirname "$0")/.." && pwd)/frames}"
fi
training="$frames/training"
mkdir -p "$training"

save_config() {
  umask 077
  printf 'ip=%s\npassword=%s\n' "$saved_ip" "$password" > "$config"
}

if [[ -z "$password" ]]; then
  read -r -s -p "Pumpkin page password (PAGE_PASSWORD in secrets.h): " password
  echo
  save_config
fi

get() { curl -sf --max-time "${2:-3}" -u "boo:$password" "http://$pumpkin$1"; }
state() { get /state; }
field() { sed -n "s/.*$1=\([-0-9]*\).*/\1/p"; }

list_types() {
  local found=0
  for dir in "$training"/*/; do
    [[ -d "$dir" ]] || continue
    found=1
    printf '  %-40s %s frames\n' "$(basename "$dir")" "$(find "$dir" -name '*.jpg' | wc -l | tr -d ' ')"
  done
  [[ $found == 1 ]] || echo "  (none yet)"
}

# Tries every address on this device's local networks; the pumpkin answers /state with the password.
find_pumpkin() {
  local prefixes
  prefixes=$(ifconfig 2>/dev/null | sed -n 's/.*inet \(addr:\)\{0,1\}\([0-9.]*\).*/\2/p' \
    | grep -E '^(192\.168|10\.|172\.(1[6-9]|2[0-9]|3[01]))' | sed 's/\.[0-9]*$//' | sort -u || true)
  if [[ -z "$prefixes" ]]; then
    read -r -p "Network to search, first three numbers (e.g. 192.168.43): " prefixes
  fi
  local result
  result=$(mktemp)
  for prefix in $prefixes; do
    echo "Searching $prefix.1-254..."
    for host in $(seq 1 254); do
      (curl -sf --max-time 2 -u "boo:$password" "http://$prefix.$host/state" 2>/dev/null | grep -q '^score=' \
        && echo "$prefix.$host" >> "$result") &
      (( host % 64 == 0 )) && wait
    done
    wait
  done
  local ip
  ip=$(head -1 "$result")
  rm -f "$result"
  if [[ -z "$ip" ]]; then
    echo "Not found. Is the pumpkin on, and on the same network? Is the password right? (edit $config)"
    exit 1
  fi
  saved_ip="$ip"
  save_config
  echo "Found the pumpkin at $ip - saved. Status page: http://$ip/"
}

case "${1:-}" in
  find)
    find_pumpkin
    exit 0 ;;
  watch)
    last=""
    echo "Watching http://$pumpkin - Ctrl+C to stop"
    while true; do
      if line=$(state); then
        score=$(echo "$line" | field score); boos=$(echo "$line" | field boos); quiet=$(echo "$line" | field quiet)
        if [[ -n "$last" && "$boos" != "$last" ]]; then
          # The bell character makes Termux vibrate (its default bell setting); a Mac terminal beeps.
          printf '\a\rBOO! person %s%%%s\n' "$score" "$([[ $quiet == 1 ]] && echo ' (quiet mode, no sound)')"
        fi
        last="$boos"
        printf '\rperson %3s%%  boos %s   ' "$score" "$boos"
      else
        printf '\rcannot reach the pumpkin   '
      fi
      sleep 0.5
    done ;;
  quiet)
    [[ "${2:-}" =~ ^(on|off)$ ]] || { echo "Usage: capture quiet on|off"; exit 1; }
    get "/quiet?on=$([[ $2 == on ]] && echo 1 || echo 0)" > /dev/null; state
    exit 0 ;;
  vol)
    [[ "${2:-}" =~ ^[0-9]+$ ]] || { echo "Usage: capture vol <0-30>"; exit 1; }
    get "/volume?level=$2" > /dev/null; state
    exit 0 ;;
  ''|-h|--help)
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    exit 0 ;;
  list|ls)
    echo "Types in $training:"
    list_types
    exit 0 ;;
esac

type="$1"
if [[ ! "$type" =~ ^[a-z0-9-]+$ ]]; then
  echo "Use lowercase letters, numbers and hyphens, e.g. busy-makerspace-empty"
  exit 1
fi

dir="$training/$type"
if [[ ! -d "$dir" ]]; then
  echo "'$type' is a new type. Existing types:"
  list_types
  read -r -p "Create training/$type? [y/N] " answer
  [[ "$answer" =~ ^[Yy]$ ]] || { echo "Not created."; exit 1; }
  mkdir -p "$dir"
fi

get /state > /dev/null \
  || { echo "Cannot reach http://$pumpkin - run 'capture find', or check the password in $config"; exit 1; }

echo "Saving to training/$type every ${gap}s - Ctrl+C to stop"
saved=0
trap 'echo; echo "Saved $saved frames to training/$type"; exit 0' INT
while true; do
  frame="$dir/$(date +%Y%m%d-%H%M%S)-$RANDOM.jpg"
  if get /capture 5 > "$frame"; then
    saved=$((saved + 1))
    printf '\r%d frames, person %s%%   ' "$saved" "$(state | field score)"
  else
    rm -f "$frame"
    printf '\rmissed a frame'
  fi
  sleep "$gap"
done

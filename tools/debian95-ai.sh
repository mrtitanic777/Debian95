#!/bin/sh
# Debian95 AI backend: one-shot chat generation with the bundled local model via
# llama.cpp. Reads a ChatML prompt file, writes just the assistant's reply.
#   debian95-ai <prompt-file> <output-file>
M=/usr/share/debian95/ai/model.gguf
CLI=/usr/local/bin/llama-cli
if [ ! -f "$M" ] || [ ! -x "$CLI" ]; then
  echo "(Clippy's brain isn't installed on this machine yet.)" > "$2"
  exit 1
fi
# leave one core for the desktop so the UI stays responsive while Clippy thinks
NP="$(nproc 2>/dev/null || echo 2)"; N=$(( NP > 1 ? NP - 1 : 1 ))
"$CLI" -m "$M" -f "$1" -n 120 -c 1024 -t "$N" --simple-io --log-disable < /dev/null 2>/dev/null \
  | awk '/<\|im_start\|>assistant/{buf="";on=1;next} on{buf=buf $0 "\n"} END{printf "%s",buf}' \
  | sed -E 's/<\|im_end\|>.*//; /^\[ (Prompt|Generation|EOG|Context)/d; /^Exiting/d; /^Loading model/d; /^>[[:space:]]*$/d' \
  | sed -e 's/[[:space:]]*$//' \
  | sed -e '/./,$!d' \
  > "$2"
[ -s "$2" ] || echo "(Hmm, I didn't catch that - try again?)" > "$2"

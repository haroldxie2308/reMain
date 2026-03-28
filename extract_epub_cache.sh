#!/bin/sh
set -eu

BASE="/home/root/.local/share/remarkable/xochitl"
CACHE_ROOT="/home/root/.cache/translate-plugin-epub"
CONF="/home/root/.config/remarkable/xochitl.conf"

doc_id="${1:-}"
if [ -z "$doc_id" ]; then
    doc_id="$(grep -m1 '^LastOpen=' "$CONF" | cut -d= -f2)"
fi

if [ -z "$doc_id" ]; then
    echo "No document id found" >&2
    exit 1
fi

epub="$BASE/$doc_id.epub"
if [ ! -f "$epub" ]; then
    echo "EPUB not found: $epub" >&2
    exit 1
fi

doc_cache="$CACHE_ROOT/$doc_id"
mkdir -p "$doc_cache"

unzip -l "$epub" | awk 'NR > 3 { print $4 }' | while IFS= read -r entry; do
    case "$entry" in
        OEBPS/Text/*.html|OEBPS/Text/*.xhtml)
            out="$doc_cache/$entry.blocks.txt"
            mkdir -p "$(dirname "$out")"
            unzip -p "$epub" "$entry" \
                | awk '
                    function trim(s) {
                        sub(/^[[:space:]]+/, "", s)
                        sub(/[[:space:]]+$/, "", s)
                        return s
                    }

                    function decode(s) {
                        gsub(/\r/, "", s)
                        gsub(/\n+/, " ", s)
                        gsub(/&nbsp;/, " ", s)
                        gsub(/&amp;/, "\\&", s)
                        gsub(/&lt;/, "<", s)
                        gsub(/&gt;/, ">", s)
                        gsub(/&#39;/, "\047", s)
                        gsub(/&quot;/, "\"", s)
                        gsub(/[[:space:]][[:space:]]*/, " ", s)
                        return trim(s)
                    }

                    function flush_block(text) {
                        print decode(text) "\n"
                    }

                    BEGIN {
                        RS = "<"
                        ORS = ""
                        in_block = 0
                        block = ""
                    }
                    {
                        tag = ""
                        txt = $0
                        if (NR > 1) {
                            pos = index($0, ">")
                            if (pos > 0) {
                                tag = substr($0, 1, pos - 1)
                                txt = substr($0, pos + 1)
                            } else {
                                tag = $0
                                txt = ""
                            }
                        }
                        lower = tolower(tag)

                        if (lower ~ /^(p|div|h1|h2|h3|li)([[:space:]>]|$)/) {
                            if (in_block)
                                flush_block(block)
                            in_block = 1
                            block = ""
                        }

                        if (in_block && txt != "")
                            block = block txt " "

                        if (in_block && lower ~ /^br[[:space:]\/>]*$/)
                            block = block " "

                        if (in_block && lower ~ /^\/(p|div|h1|h2|h3|li)[[:space:]>]*$/) {
                            flush_block(block)
                            in_block = 0
                            block = ""
                        }
                    }
                    END {
                        if (in_block)
                            flush_block(block)
                    }
                ' \
                > "$out"
            ;;
    esac
done

echo "$doc_cache"

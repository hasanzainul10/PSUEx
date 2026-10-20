#!/usr/bin/env bash
# Print the last PSUEx session from the game's psuex.log (everything after the
# last "==== PSUEx attach ===="), filtered and with repeated lines collapsed.
#
#   tools/lastlog.sh [-f LOG] [-n MAX] [-a] [-d] [PATTERN]
#
#   PATTERN  extended regex to keep (e.g. 'D3D9ExCompat|VREffect:'); default: all lines
#   -n MAX   print at most MAX output lines (default 200, 0 = no limit)
#   -a       keep every line in order (no collapsing)
#   -d       collapse lines that differ only in numbers / hex (frame counters, pointers)
#   -s       also print the session's line count and time span
#   -f LOG   log file (default D:/Games/PSUClementine_22072025/psuex.log)
#
# Collapsed output is "COUNT  line", in order of first appearance.

log="/d/Games/PSUClementine_22072025/psuex.log"
max=200
all=0
digits=0
stats=0

while getopts "f:n:adsh" opt; do
	case "$opt" in
		f) log="$OPTARG" ;;
		n) max="$OPTARG" ;;
		a) all=1 ;;
		d) digits=1 ;;
		s) stats=1 ;;
		*) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	esac
done
shift $((OPTIND - 1))
pattern="${1:-}"

if [ ! -f "$log" ]; then
	echo "lastlog: no log at $log" >&2
	exit 1
fi

start=$(grep -n -a -F "==== PSUEx attach ====" "$log" | tail -1 | cut -d: -f1)
start=${start:-1}

session() { tail -n +"$start" "$log" | tr -d '\r'; }

if [ "$stats" = 1 ]; then
	total=$(session | wc -l)
	echo "lastlog: session starts at line $start, $total lines"
fi

filter() {
	if [ -n "$pattern" ]; then grep -a -E -- "$pattern"; else cat; fi
}

collapse() {
	if [ "$all" = 1 ]; then
		cat
		return
	fi
	awk -v digits="$digits" '
		{
			key = $0
			if( digits ) { gsub( /0x[0-9A-Fa-f]+|[0-9A-Fa-f]{8}|[0-9]+(\.[0-9]+)?/, "#", key ) }
			if( !( key in count ) ) { order[ ++n ] = key; first[ key ] = $0 }
			count[ key ]++
		}
		END {
			for( i = 1; i <= n; i++ ) { k = order[ i ]; printf "%6d  %s\n", count[ k ], first[ k ] }
		}'
}

limit() {
	if [ "$max" -gt 0 ] 2>/dev/null; then
		awk -v max="$max" 'NR <= max { print } END { if( NR > max ) printf "... (%d more lines, raise -n or narrow the pattern)\n", NR - max }'
	else
		cat
	fi
}

session | filter | collapse | limit

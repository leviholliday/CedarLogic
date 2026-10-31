#!/usr/bin/env bash
# Back up or restore the two update-signing keys that live only in this Mac's Keychain.
#
#   scripts/signing-keys.sh backup     both keys -> one passphrase-locked file in iCloud Drive
#   scripts/signing-keys.sh restore    that file -> both keys back into the login Keychain
#   scripts/signing-keys.sh check      is each key in the Keychain, and is there a backup?
#
# The keys:
#   - Mac updates (Sparkle EdDSA): account "cedarlogic", service https://sparkle-project.org.
#     publish-update.sh signs with it.
#   - Windows and Linux test builds (Ed25519 PEM): account "cedarlogic-preview".
#     sign-preview.sh signs with it.
# Neither can be replaced: the apps out there trust only these keys, so losing them means
# no installed copy can ever be updated again. Hence a backup off this Mac.
#
# The backup is AES-256 with a key stretched from your passphrase (PBKDF2, 600,000 rounds),
# made with the openssl every Mac has, so restoring needs nothing installed. The keys never
# touch the disk unencrypted. Keep the passphrase in the Passwords app; without it the file
# is useless, to you and to anyone else. A copy of this script sits next to the backup, so
# on a new Mac:   bash ~/Library/Mobile\ Documents/com~apple~CloudDocs/CedarLogic\ Keys/signing-keys.sh restore
#
# macOS asks once per key to let "security" read it: click Always Allow (or Allow).

set -euo pipefail

SPARKLE_ACCOUNT=cedarlogic
SPARKLE_SERVICE=https://sparkle-project.org
SPARKLE_LABEL="Private key for signing Sparkle updates"
PREVIEW_ACCOUNT=cedarlogic-preview
PREVIEW_SERVICE="CedarLogic preview update signing (Ed25519, PEM)"
PREVIEW_PUBHEX=4184daba2830c1db416be7869a7d873788170ea1c00104abcbfd018d7fe3cf40 # as in sign-preview.sh

DIR=${CEDARLOGIC_KEYS_DIR:-"$HOME/Library/Mobile Documents/com~apple~CloudDocs/CedarLogic Keys"}
BACKUP="$DIR/cedarlogic-signing-keys.enc"
OSSL=/usr/bin/openssl
CIPHER=(enc -aes-256-cbc -pbkdf2 -iter 600000 -md sha256 -salt)

die() { echo "signing-keys: $*" >&2; exit 1; }
say() { echo "signing-keys: $*"; }

# The passphrase into $PASS, asked here (nothing shows as you type), so openssl gets it
# through the environment instead of its own terse prompt.
ask_pass() {
	local again
	while :; do
		read -r -s -p "Passphrase (nothing shows as you type): " PASS </dev/tty; echo
		[ "${1:-}" = new ] || break
		[ ${#PASS} -ge 8 ] || { say "use at least 8 characters"; continue; }
		read -r -s -p "Same passphrase again: " again </dev/tty; echo
		[ "$PASS" = "$again" ] && break
		say "those two didn't match; try again"
	done
	export PASS
}

# A Keychain item's secret as `security -w` prints it (hex when it isn't plain text).
read_item() { security find-generic-password -a "$1" -s "$2" -w 2>/dev/null; }

# The preview key's public half, as hex, when an OpenSSL that knows Ed25519 is around.
preview_pubhex() {
	local raw=$1 ossl pem
	for ossl in "${OPENSSL:-}" /opt/homebrew/bin/openssl /usr/local/bin/openssl; do
		[ -n "$ossl" ] && [ -x "$ossl" ] || continue
		if [[ "$raw" == -----BEGIN* ]]; then pem=$raw; else pem=$(printf '%s' "$raw" | xxd -r -p); fi
		printf '%s\n' "$pem" | "$ossl" pkey -pubout -outform DER 2>/dev/null | tail -c 32 | xxd -p -c 64 && return
	done
	return 1
}

backup() {
	local sparkle preview
	sparkle=$(read_item "$SPARKLE_ACCOUNT" "$SPARKLE_SERVICE") || die "no Mac (Sparkle) key in the Keychain"
	preview=$(read_item "$PREVIEW_ACCOUNT" "$PREVIEW_SERVICE") || die "no Windows/Linux preview key in the Keychain"
	mkdir -p "$DIR"
	say "choose a passphrase and save it in the Passwords app as \"CedarLogic signing keys backup\""
	ask_pass new
	# Plain text goes straight from here into openssl's pipe, never to disk.
	printf 'sparkle %s\npreview %s\n' "$sparkle" "$preview" | "$OSSL" "${CIPHER[@]}" -pass env:PASS -out "$BACKUP.new" \
		|| { rm -f "$BACKUP.new"; die "openssl didn't write the backup"; }
	sparkle= preview=
	mv -f "$BACKUP.new" "$BACKUP"
	cp -f "$0" "$DIR/signing-keys.sh"
	say "saved $BACKUP"
	"$OSSL" "${CIPHER[@]}" -d -pass env:PASS -in "$BACKUP" | grep -c '^\(sparkle\|preview\) ' | grep -qx 2 \
		&& say "the backup opens and holds both keys" \
		|| die "the backup didn't open again; run backup again"
	PASS=
}

restore() {
	local plain sparkle preview have got
	[ -f "$BACKUP" ] || die "no backup at $BACKUP (is iCloud Drive done downloading it?)"
	say "type the backup's passphrase (it's in the Passwords app)"
	ask_pass
	plain=$("$OSSL" "${CIPHER[@]}" -d -pass env:PASS -in "$BACKUP" 2>/dev/null | tr -d '\000') || die "wrong passphrase, or the file is damaged"
	sparkle=$(printf '%s\n' "$plain" | sed -n 's/^sparkle //p')
	preview=$(printf '%s\n' "$plain" | sed -n 's/^preview //p')
	plain= PASS=
	[ -n "$sparkle" ] && [ -n "$preview" ] || die "the backup opened but doesn't hold both keys"
	if got=$(preview_pubhex "$preview"); then
		[ "$got" = "$PREVIEW_PUBHEX" ] || die "the preview key in the backup isn't the one the apps trust; not restoring"
	fi

	if have=$(read_item "$SPARKLE_ACCOUNT" "$SPARKLE_SERVICE"); then
		[ "$have" = "$sparkle" ] && say "Mac key: already in the Keychain" \
			|| die "a DIFFERENT Mac key is already in the Keychain; not touching it (delete it in Keychain Access first if you're sure)"
	else
		security add-generic-password -a "$SPARKLE_ACCOUNT" -s "$SPARKLE_SERVICE" -l "$SPARKLE_LABEL" \
			-D "private key" -w "$sparkle" && say "Mac key: restored"
	fi

	if have=$(read_item "$PREVIEW_ACCOUNT" "$PREVIEW_SERVICE"); then
		[ "$have" = "$preview" ] && say "Windows/Linux key: already in the Keychain" \
			|| die "a DIFFERENT preview key is already in the Keychain; not touching it"
	else
		# `security -w` printed it as hex because it's several lines: put the same bytes back.
		if [[ "$preview" == -----BEGIN* ]]; then set -- -w "$preview"; else set -- -X "$preview"; fi
		security add-generic-password -a "$PREVIEW_ACCOUNT" -s "$PREVIEW_SERVICE" -l "$PREVIEW_SERVICE" "$@" \
			&& say "Windows/Linux key: restored"
	fi
	sparkle= preview=
}

check() {
	read_item "$SPARKLE_ACCOUNT" "$SPARKLE_SERVICE" >/dev/null && say "Mac key: in the Keychain" || say "Mac key: MISSING"
	read_item "$PREVIEW_ACCOUNT" "$PREVIEW_SERVICE" >/dev/null && say "Windows/Linux key: in the Keychain" || say "Windows/Linux key: MISSING"
	if [ -f "$BACKUP" ]; then say "backup: $BACKUP ($(stat -f '%Sm' "$BACKUP"))"; else say "backup: none yet (run: $0 backup)"; fi
}

case "${1:-}" in
	backup) backup ;;
	restore) restore ;;
	check) check ;;
	*) sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac

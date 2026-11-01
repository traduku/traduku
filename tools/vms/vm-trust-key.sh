#!/usr/bin/env bash
# Runs INSIDE the Arch grid VM as vm: trust the dev-channel signing key in
# the pacman keyring. --lsign-key needs a secret key (and hangs on pinentry);
# ultimate ownertrust is the batch-safe equivalent for a key we ship.
set -u
ASC=$(ls ~/traduku-dev-pub.asc 2>/dev/null) || { echo "no key file"; exit 1; }
echo vm | sudo -S pacman-key --add "$ASC" >/dev/null 2>&1
FPR=$(echo vm | sudo -S gpg --homedir /etc/pacman.d/gnupg --list-keys \
      --with-colons traduku-dev@localhost 2>/dev/null | awk -F: '/^fpg?r/{print $10; exit}')
[ -n "$FPR" ] || { echo "key not in keyring"; exit 1; }
# ownertrust is set by root in a single sudo (gpg reads commands from the pipe)
echo vm | sudo -S sh -c "printf 'trust\n5\ny\nquit\n' | gpg --homedir /etc/pacman.d/gnupg --batch --command-fd 0 --yes --edit-key $FPR >/dev/null 2>&1"
echo vm | sudo -S pacman-key --updatedb >/dev/null 2>&1
echo "trusted: $FPR"

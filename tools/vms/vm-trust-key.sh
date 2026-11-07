#!/usr/bin/env bash
# Runs INSIDE the Arch grid VM as vm: trust the signing keys the grid's packages
# carry, in the pacman keyring. --lsign-key needs a secret key (and hangs on
# pinentry); ultimate ownertrust is the batch-safe equivalent for keys we ship.
#
# Both keys, because there are two channels: the development key signs the grid's
# own builds, the release key signs what the release page ships - and the grid
# installs release artifacts exactly as a user downloads them, so it has to
# accept that signature too. Arch refuses an unverifiable one outright: without
# this, `pacman -U` answers "package missing required signature" or "invalid or
# corrupted package (PGP signature)", the install silently changes nothing, and
# the suite goes on testing whatever was installed last week.
set -u
ASC=$(ls ~/traduku*.asc 2>/dev/null) || { echo "no key file in ~"; exit 1; }
for key in $ASC; do
  echo vm | sudo -S pacman-key --add "$key" >/dev/null 2>&1
done
echo vm | sudo -S pacman-key --updatedb >/dev/null 2>&1
for uid in traduku-dev@localhost release@traduku.dev; do
  FPR=$(echo vm | sudo -S gpg --homedir /etc/pacman.d/gnupg --list-keys \
        --with-colons "$uid" 2>/dev/null | awk -F: '/^fpr/{print $10; exit}')
  [ -n "$FPR" ] || { echo "FAIL: no key for $uid in the pacman keyring" >&2; exit 1; }
  # ownertrust is set by root in one sudo: gpg reads the commands from the pipe
  echo vm | sudo -S sh -c "printf 'trust\n5\ny\nquit\n' | gpg --homedir /etc/pacman.d/gnupg --batch --command-fd 0 --yes --edit-key $FPR >/dev/null 2>&1"
  echo "trusted: $uid $FPR"
done

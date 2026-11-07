#!/usr/bin/env bash
# One-time host setup for signing the Arch release packages (the grid VMs
# trust the exported public key; users would do the same for a dev channel).
# The private key lives in ~/.gnupg only — never in the repo.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
FPR=$(gpg --list-keys --with-colons traduku-dev@localhost 2>/dev/null | awk -F: '/^fpr/{print $10; exit}' || true)
if [ -z "$FPR" ]; then
  gpg --batch --gen-key <<'KEY'
%no-protection
Key-Type: RSA
Key-Length: 3072
Subkey-Type: RSA
Subkey-Length: 3072
Name-Real: Traduku Release (dev channel)
Name-Email: traduku-dev@localhost
Expire-Date: 10y
%commit
KEY
  FPR=$(gpg --list-keys --with-colons traduku-dev@localhost | awk -F: '/^fpr/{print $10; exit}')
fi
mkdir -p "$HOME/.config/pacman"
grep -q traduku-dev "$HOME/.makepkg.conf" 2>/dev/null || cat >> "$HOME/.makepkg.conf" <<MKP
sign=yes
gpgkey=${FPR: -16}
MKP
gpg --armor --export "$FPR" > "$ROOT/packaging/arch/traduku-dev-pub.asc"
echo "key: $FPR; public key exported to packaging/arch/traduku-dev-pub.asc"

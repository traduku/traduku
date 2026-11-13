#!/usr/bin/env bash
# Gate: every shell that builds a tray must connect the tray's language pick.
#
# Why a gate and not a test. The tray's "Translate To" fires
# ITrayBackend::targetLanguagePicked, and a pick has a consequence: the
# destination language is saved and the text in the window is translated into
# it (ROADMAP.md §7.8 item 4). That consequence lives in ONE place,
# AppController::pickTargetLanguage, and it is reached by a connect in each
# shell. There are three shells - windows, kde, gnome - three separate .cpp
# files, and the compiler checks none of them against the others: a shell can
# create a QtTray, wire translate/settings/quit, and leave the language row
# emitting into the void. That is exactly the state this product was in, and it
# was found by a person clicking the menu, which is the most expensive way to
# find anything.
#
# The gate asks one question per shell: do you build a QtTray? then connect
# targetLanguagePicked to the controller. It does not check order, thread, or
# context object - those are tested by tst_appcontroller, which runs the real
# controller. This file only refuses the silent case: a menu entry that leads
# nowhere in the build a user happens to run.
set -u
cd "${1:-.}" || exit 2

status=0
for main in src/app/windows/main.cpp src/app/kde/main.cpp src/app/gnome/main.cpp; do
    [ -f "$main" ] || { echo "missing shell: $main"; status=1; continue; }

    # A shell that does not build a tray at all is not in this contract
    # (TRADUKU_TRAY=none, --selftest, the CLI modes): grepping for the connect
    # in a file that never mentions QtTray would demand a connect to an object
    # that does not exist.
    if ! grep -q "QtTray" "$main"; then
        echo "skip  $main (no tray built here)"
        continue
    fi

    if grep -A2 "targetLanguagePicked" "$main" | grep -q "pickTargetLanguage"; then
        echo "ok    $main"
    else
        echo "FAIL  $main builds a QtTray and does not connect"
        echo "      &QtTray::targetLanguagePicked to &AppController::pickTargetLanguage:"
        echo "      the tray's language menu is a row that leads nowhere in this shell."
        status=1
    fi
done
exit $status

Stage 3A закрито: живий цикл «жест → буфер → переклад → вікно» підтверджений оператором на GNOME 50.1/Wayland.

План:

 Stage 4 — X11Clipboard + ланцюжок пріоритетів у ClipboardFactory (наступний, ~1 сесія)
 Зараз на Wayland у нас wl-clipboard, а на X11 — сліпий Qt. Треба XConvertSelection (у вимірюваннях 10 мс) і порядок wl-paste → X11 → Qt, з спільним парсером
 MIME/charset (UTF8_STRING, TEXT, TIMESTAMP…). Це єдине, що закриває обіцянку «працює на Ubuntu 24.04 / Debian 13», і воно не залежить від VM.

 Stage 5 — SniTray (~1–2 сесії, чистий Qt D-Bus SNI + com.canonical.dbusmenu)
 Одна реалізація трея для обох оболонок замість KdeTray-ли лише. На GNOME без розширення іконки не буде й далі — це і так вимога (трей-less вже працює), але з
 розширенням AppIndicator має з'явитися повне меню.

 Stage 6 — доопрацювання досвіду + 6b (~1 сесія)
 First-run (перший запуск пояснює, що натискати, бо іконки немає), автозапуск, HiDPI, PipeWire-звуки. 6b: прогнати SniTray на arch-kde з LT_TRAY=sni, і лише
 після зеленого e2e + вашого «ок» прибирати KdeTray. Це тепер розблоковано — KF6 у вас установлений (29 пакетів), чого не було вчора.

 Stage 7 — e2e-сітка (найдовша і найризыковіша частина)
 tests/e2e/profiles.py (KdeProfile/GnomeProfile), тести 09–13, зокрема test_09_gesture_evdev через QMP-надсилання клавіш; mkvm.sh зараз пише WaylandEnable=False
 — треба вміти й GNOME Wayland. Потім ~/vms/ підняти (setup-ctr.sh + vmctl.sh up + mkvm) і тримати TRADUKU_VM=arch-kde pytest tests/e2e 8/8 та vm-functest.sh 7/7
 ×2 — це ворота перед мержем у master.

 Stage 8 — пакування (~1 сесія)
 Обов'язково -DLT_SHELL=gnome у packaging/debian/rules (вчора дізнався, чому: auto бере KDE, щойно в системі є KF6 — так і стало на цій машині), прибрати
 KF6-zалежності з GNOME-пакета, Recommends: wl-clipboard + індикатор-розширення, поставити udev-правило в пакет (саме 70-* — імені завантажене: пізніше за
 71-seat.rules не працює), README/ARCHITECTURE, версія 0.3.0.

 Потім — рефакторинг KDE-head (src/app/kde/main.cpp досі дублює парсер з cli.cpp) і мерж integration/gnome-base → master.

 Що я свідомо відклав

 - PortalGlobalShortcut — портал вертає portal.Error.NotAllowed: An app id is required у всіх п'яти спробах з ідентичністю; тестувати на фейковому порталі =
   доводити, що фейк поводиться як фейк.
 - GestureService на D-Bus і shell-розширення GNOME — не потрібні, поки evdev дає справжній Ctrl+C+C.
 - Stage 3B (GsettingsGlobalShortcut + --trigger) — єдиний пункт, який я готовий спіроти: evdev уже працює, а глобальний шорткат через gsettings це фактично
   запасний хід дляbox-ів, де input недоступний. Якщо скажете «не треба» — знімаю зі плану.

 Що потребує вашого рішення (не мою роботу, а дозвіл)

 1. Писати у ваші GNOME-налаштування (custom keybinding через gsettings) — нав'язливо чи ок, якщо тільки за галочкою у first-run?
 2. udev-правило в .deb — це системний файл /lib/udev/rules.d/70-traduku-uaccess.rules. Я за, але це ваш дозвіл на рівень «пакет міняє ACL клавіатури». Додам у
    README рядок про те, що саме воно робить (і чому input-група — поганий варіант).
 3. Видалення KdeTray — тільки після 6b і вашого «ок».

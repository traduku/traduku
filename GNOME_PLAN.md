# Traduku — план GNOME-версии

Цель: второй composition root (`app/gnome`) + слой `platform/gnome`, чтобы
`traduku` работал на GNOME **Wayland** и X11 **без KF6**, с тем же
пользовательским обещанием, что и на KDE: **Ctrl+C+C переводит буфер**,
буфер читается и в него пишется результат, работает автозапуск и звук.
Ядро (`core/`) и UI (`ui/`) не трогаем — они DE-нейтральные
(ARCHITECTURE.md). Правило TDD из ARCHITECTURE.md действует: кусок
продуктового кода без падающего теста не принимается.

Хост ручной проверки: **Ubuntu 26.04 LTS, GNOME Shell 50.1, Wayland**
(`ubuntu26`, ядро 7.0.0-34).

## Решения, принятые оператором (2026-11-04)

| # | Решение | Как отражено в плане |
|---|---|---|
| 1 | **Ctrl+C+C должен работать везде** — и на X11, и на Wayland | D1: кандидат, который это даёт на Wayland, — `EvdevHotkeyBackend` (чтение `/dev/input`, ничего не перехватывает). Портал/gsettings остаются запасными путями с другим жестом |
| 1b | X11 нужен в основном старым ОС (Ubuntu 24.04 Xorg, Debian 13) | XI2/RECORD-слой остаётся (`LT_BUILD_X11=ON`), в матрице — один X11-джоб как свойство сессии |
| 2 | Главное — жест + чтение/запись буфера. AppIndicator-расширение добавить, но **программа обязана работать без него** | D3: расширение — рекомендация в README и в `--backend-info`; отсутствие watchера = деградация без падения, с явной строкой в логе |
| 3 | gsettings-fallback — сразу, только если портал в текущем окружении не подтвердится | D1-B: правило решения привязано к спайку Stage 0, результат записывается в §9 |
| 4 | `KdeTray` не удалять, пока `SniTray` не проверен в KDE | Stage 5 = SniTray на GNOME; Stage 6b = прогон `SniTray` на arch-kde (e2e 8/8 + руками), удаление `KdeTray` — отдельным коммитом **после** зелёного прогона и вашего «да» |
| 5 | Схема мержа веток одобрена | §7; уже выполнено: `integration/gnome-base` = `pipeline` + `debian`, мерж-коммит `3fc9f6a` |
| 6 | Build-deps ставим локально | §8 (команды для `apt`) |

---

## 0. Замеры на реальном хосте (2026-11-04, Ubuntu 26.04 / GNOME 50.1 / Wayland)

Надёжные (проверено напрямую: `busctl`, `gdbus`, `gsettings`, `dpkg`, `ls`):

| Что | Факт | Значение |
|---|---|---|
| `org.kde.StatusNotifierWatcher` | **есть**, `IsStatusNotifierHostRegistered=true` (расширение `ubuntu-appindicators@ubuntu.com` включено) | трей на GNOME реален; в реестре сейчас только ayatana-иконки apt |
| `org.freedesktop.Notifications` | есть | `INotifier` = прямой D-Bus, libnotify не нужен |
| `org.freedesktop.portal.GlobalShortcuts` | реализован в `xdg-desktop-portal-gnome` 50.0 (front-door + impl-интерфейс на шине) | запасной жест без спецправ; `Activated`/`Deactivated` = press/release |
| `org.freedesktop.portal.Clipboard` | есть, но по спецификации работает **только над сессиями RemoteDesktop/InputCapture** | для чтения буфера обычным приложением не годится |
| `org.gnome.Mutter.Clipboard` | **нет** на шине | готового «прочитай буфер из фона» API в GNOME 50 нет |
| `/dev/input/event*` | `crw-rw---- root:input`; группы `input` у пользователя **нет** | evdev-жест требует ACL; правильный путь — udev `uaccess` (проверено, см. D1), а не группа. **Ловушка:** файл обязан именоваться `70-*` (до `71-seat.rules`/`73-seat-late.rules`, где `TAG=="uaccess"` кудит `RUN{builtin}+="uaccess"`) и тригериться с `--action=add`; `99-*` вешает тег слишком поздно — ACL не появляется, хотя `udevadm info` показывает `:uaccess:` |
| `org.gnome.SettingsDaemon.MediaKeys` | есть, `custom-keybindings` пуст | gsettings-fallback возможен |
| `wl-clipboard`, `xclip` | не установлены | Stage 0 их ставит: без них `WlClipboard` недоступен |
| Qt6/KF6 dev | не установлены | §8 |
| qemu на хосте | нет; docker есть; `~/vms` пуст (мы в Ubuntu, грид жил на Arch-стороне) | грид поднимается заново: `vmctl.sh up` + `mkvm` |

### 0b. Результаты Stage 0 (замерено 2026-11-04, `tools/spike/`)

**Буфер на GNOME 50 Wayland — вопрос закрыт** (`clip-spike probe`, в буфере
лежал реальный текст, скопированный пользователем из браузера):

| путь | результат | что значит |
|---|---|---|
| A `QClipboard::text()` из фонового wayland-клиента | **ПУСТО** (251 мс) | Qt-путь на муттере для фонового приложения мёртв |
| B то же после `show()+requestActivate()` | **ПУСТО** (351 мс) | фон не получает фокус → focus-window трюк бесполезен |
| C `wl-paste` / `wl-paste -l` | **OK**, 13–18 мс, типы `text/plain text/plain;charset=utf-8 STRING TEXT` | **`WlClipboard` (уже есть в дереве) работает на GNOME** |
| C2 `wl-copy` из фона | **OK** — selection принадлежит фоновому процессу | запись результата тоже работает без фокуса |
| E `XConvertSelection` (raw Xlib к XWayland `:0`) | **OK**, 10 мс | запасной путь без внешних пакетов |
| D Qt на `QT_QPA_PLATFORM=xcb` | **OK** (A и B работают) | XWayland-клиент читает буфер из фона — X11-семантика |

Отсюда D2 решается без расширения Shell и без focus-трюка: **основной
`WlClipboard` + `wl-clipboard` в зависимостях, `X11Clipboard` как запас, к
фоновому чтению `QClipboard` на wayland не возвращаемся**.

**Портал GlobalShortcuts — для нас закрыт** (`portal-spike check`, все
попытки: `CreateSession` → `GDBus.Error.org.freedesktop.portal.Error.NotAllowed:
An app id is required`):

| попытка | итог |
|---|---|
| 1. вызов «как есть» | FAIL (app id is required) |
| 2. + владение именем `dev.traduku.Traduku` и экспорт `org.freedesktop.Application.ApplicationId` | FAIL |
| 3. + `app_id` в options вызова | FAIL |
| 4. + установленный `dev.traduku.Traduku.desktop` | FAIL |
| 5. + запуск настоящего приложения через `gio launch` (cgroup `app-…`, как у нормальных приложений) | FAIL |

Вывод: `xdg-desktop-portal` 1.21.1 + `xdg-desktop-portal-gnome` 50 даёт
GlobalShortcuts только **sandboxed** вызывающим (flatpak/snap). Правило
решения 3 сработало: **`GsettingsGlobalShortcut` пишем сразу**, портал —
отдельная ветка «если когда-нибудь разблокируют» (пере-проверить спайком).

**evdev — ждёт разрешения**: 24 устройства `/dev/input/event*`, все
`Permission denied` (группы `input` нет). Команды и повторный прогон —
`./tools/spike/stage0.sh`.

---

## 1. Gap-анализ по интерфейсам

| Интерфейс | KDE (готово) | GNOME (делать) |
|---|---|---|
| `IHotkeyBackend` | `X11HotkeyBackend` (XI2 raw + RECORD-хелпер) | он же на X11-сессии + **новый `EvdevHotkeyBackend`** на Wayland (D1) |
| `IGlobalShortcut` | `KdeGlobalShortcut` (KGlobalAccel) | `PortalGlobalShortcut` + `GsettingsGlobalShortcut` (D1-B/C) |
| `IClipboard` | `ClipboardFactory` → Qt/Wl | те же + **`X11Clipboard`** (чтение/запись через XWayland) и, опционально, мост через расширение Shell (D2) |
| `INotifier` | `KdeNotifier` (+notifyrc) | `FdoNotifier` — прямой `org.freedesktop.Notifications` (D4) |
| `IAutostart` | `XdgAutostart` | он же (GNOME уважает XDG autostart) |
| трей | `KdeTray` (KF6) | `SniTray` — чистый D-Bus SNI + `com.canonical.dbusmenu` (D3); общий для KDE и GNOME |
| один экземпляр | `KDBusService(Unique)` | `SingleInstance` (QLocalServer + сессионное имя `dev.traduku.Traduku`) (D5) |
| composition root | `app/kde/main.cpp` | `app/gnome/main.cpp`; общий парсер CLI → `app/cli.*` |
| id приложения | — | `QCoreApplication::setApplicationName` + `QGuiApplication::setDesktopFileName("traduku")` + установленный `traduku.desktop` (нужно порталу, уведомлениям и иконке окна) |

---

## 2. D1 — жест Ctrl+C+C везде

Продуктовая правда, которую надо принять: на Wayland **никакой** легальный
механизм не позволяет пассивно «слушать» нажатия чужих окон — compositor
не отдаёт клавиши клиентам. Значит наблюдение либо (а) ниже композитора —
в ядре, либо (б) внутри композитора — портал/расширение/gsettings, ценой
другого сочетания клавиш.

**Кандидат 1 (основной, даёт именно Ctrl+C+C): `EvdevHotkeyBackend`,
`src/platform/evdev/`.** Чтение `/dev/input/event*` (`EV_KEY`), никаких
grabs и инъекций → копирование пользователем не трогается вообще; работает
одинаково под GNOME и KDE на Wayland и на X11 (XI2 остаётся запасным).
Сообщение клавиш — физическое: `KEY_LEFTCTRL`/`KEY_RIGHTCTRL` + `KEY_C`,
раскладка не важна (та же дисциплина, что у XI2-бэкенда, где мы сверяем
native keycode).

* Цена: нужен доступ к `/dev/input/event*`. **Не группа `input`** (она даёт
  умение читать клавиатуру вообще всем программам аккаунта; в Arch-сообществе
  это называют «a sledgehammer to insert a pushpin» и советуют uaccess).
  Правильный механизм — правило udev с `TAG+="uaccess"`: udevd+logind выдают
  ACL **только активной seat-сессии**. Проверено на этой машине: systemd 259,
  `getfacl /dev/video0` показывает `user:user:rw-` (механизм работает), а
  дистрибутивный `/usr/lib/udev/rules.d/70-uaccess.rules` тегает джойстики,
  но **не клавиатуры** — потому их и не прочитать. Наше правило
  (`packaging/udev/70-traduku-uaccess.rules`) валидировано без root:
  `udevadm test --extra-rules-dir=<dir> $(udevadm info -q path -n
  /dev/input/event3)` → `TAGS=:uaccess:...`. Ставит его пользователь сам
  (или `traduku --grant-keyboard-access`, Stage 3): наблюдать клавиатуру —
  приватное решение, в `/etc` из пакета оно не едет.
* Дизайн (по образцу `X11HotkeyBackend`): рабочий поток, `QSocketNotifier`
  на каждый fd, точка входа `feedEvent(type, code, value)` для тестов,
  `DeviceFinder`-функтор вместо реального сканирования `/dev/input`
  (юнит-тест кормит синтетическими событиями), горячее подключение устройств
  — пересканирование по `inotify` на каталоге, интервал берётся из
  `Settings::pressInterval()` и обновляется по `Settings::changed`.
* Порядок выбора бэкендов на Wayland: `EvdevHotkeyBackend` → (если
  `/dev/input` не читается) портал (кандидат 3) → gsettings (кандидат 4).
  На X11-сессии: XI2/RECORD (он ничего не требует), потом evdev.
* e2e-совместимость: QMP `input-send-event` из грида — это настоящие
  события ядра, evdev их видит (в отличие от XI2-raw на Wayland, который
  измерен как пустой). То есть `test_05` для GNOME пишется естественно.

**Кандидат 2: XI2 passive (есть)** — работает только на X11-сессиях;
держим ради старых ОС и X11-джоба в матрице (решение 1b).

**Кандидат 3 (без спецправ, но не Ctrl+C+C): `PortalGlobalShortcut` —
заблокирован на этом стеке** (замеры в §0b): `CreateSession` отвечает
`portal.Error.NotAllowed: An app id is required`, а идентичность
`xdg-desktop-portal` 1.21.1 выдаёт только sandboxed-вызывающим (не помогли ни
имя на шине с `org.freedesktop.Application`, ни `app_id` в options, ни
`.desktop`, ни запуск через `gio launch`). Реализацию по спецификации
(`CreateSession` → `BindShortcuts` → `Activated`/`Deactivated` →
`ShortcutMonitor`) держим под `-DLT_PORTAL_SHORTCUT=OFF` по умолчанию и
пере-проверяем спайком на более новом стеке; имя сервиса и путь при этом
инжектируются (как `ToolFinder` в `WlClipboard`), чтобы фейковый портал в
тестах был возможен.

**Кандидат 4 (последний по списку, но основной запасной путь): 
`GsettingsGlobalShortcut` — пишем СРАЗУ** (правило решения 3 сработало:
портал не подтверждён). Пишет
`org.gnome.settings-daemon.plugins.media-keys custom-keybindings` +
`…/custom0/{name,command,binding}`, `command=gdbus call --session -d
dev.traduku.Traduku -o /dev/traduku/Traduku -m
dev.traduku.Traduku.gesturePressed`; каждое нажатие — вызов метода
работающему экземпляру, пары режет тот же детектор. Раннер `gsettings`
инжектируется, `unregister()` убирает binding. Работает и на Debian 13, и
на Ubuntu 22.04.

**Кандидат 5 (опция, later):** расширение GNOME Shell, наблюдающее клавиши
и отдающее буфер по D-Bus. Даёт Ctrl+C+C без группы `input` и решает D2, но
это отдельная поддержка и `gnome-extensions install` + перезаход в сессию.
Флаг `LT_EXPERIMENT_SHELL_EXT`; возвращаемся, если evdev или D2 не устроят.

---

## 3. D2 — буфер на GNOME Wayland (РЕШЕНО спайком Stage 0, §0b)

Замерено на живой системе: фоновое чтение `QClipboard` на муттере не
работает (A и B пустые, включая вариант с `show()+requestActivate()` —
фон не получает фокус), а `wl-paste` читает за 13 мс, `XConvertSelection`
через XWayland за 10 мс, и `wl-copy` из фона становится владельцем
selection, т.е. запись результата тоже доступна.

Итоговый порядок в `ClipboardFactory`:

1. **`WlClipboard` — уже есть в дереве**, wayland + `wl-clipboard`.
   `wl-clipboard` в `Recommends` пакета и отдельная строка в README
   («на GNOME нужен wl-clipboard»). Проверить, что KDE-специфичная ветка
   `org.kde.KClipboard` не затеняет этот путь на GNOME.
2. **`X11Clipboard` (`platform/x11`)** — запас, когда `wl-clipboard`
   отсутствует, а XWayland есть: `XGetSelectionOwner`/`XConvertSelection`
   для чтения, `XSetSelectionOwner` + ответ на `SelectionRequest` для
   записи; `XAUTHORITY` из `/proc/<Xwayland-pid>/environ` (приём обкатан в
   `clip-spike`). Разбор MIME/charset — общий с `WlClipboard` (спайк
   показал реальный набор типов: `text/plain`, `text/plain;charset=utf-8`,
   `STRING`, `TEXT`, `UTF8_STRING`, `SAVE_TARGETS`, `TIMESTAMP`).
3. `QtClipboard` — X11-сессии (там он работает) и последний шанс; на
   wayland-клиенте фонового чтения от него ждать нечего.
4. Расширение Shell — в резерве только на случай системы без
   `wl-clipboard` и без XWayland.

Focus-window трюк из плана **исключён**: замер B показал, что фоновое
приложение не получает фокус, значит трюк ничего не даёт, а фокус-кражу
пользователь бы заметил.

---

## 4. D3 — трей: общий `SniTray`, KDE не ломать

`src/platform/sni/`: `SniTray` + `SniWatcher` + `DBusMenuExporter`
(`GetLayout`, `GetGroupProperties`, `Event`, `AboutToShow`), интерфейс
`org.kde.StatusNotifierItem` (Category/Id/Title/Status/IconName/ToolTip/
ItemIsMenu + `Activate`/`ContextMenu`/`SecondaryActivate`), регистрация у
`org.kde.StatusNotifierWatcher`. Сигналы — те же, что у `KdeTray`
(`translateRequested`, `settingsRequested`, `targetLanguagePicked`),
`AppController` не меняется.

Порядок по решению 4:
1. Stage 5 — `SniTray` включён в GNOME-оболочку; `KdeTray` не трогаем.
2. Stage 6b — переключатель `LT_TRAY=sni|kde` (по умолчанию `kde`),
   `SniTray` прогоняется на arch-kde: e2e `test_04` (watcher + Title +
   red-check «убили — иконка пропала») + панель глазами (`vmctl.sh vnc
   arch-kde`).
3. Только после зелёного прогона и вашего «да» — `KdeTray` и зависимость от
   `KF6StatusNotifierItem` удаляются отдельным коммитом; до того `KdeTray`
   живёт и остаётся дефолтом KDE-сборки.

Отсутствие watchера (чистый GNOME без расширения) = не ошибка:
`tray=none (no StatusNotifierWatcher)` в `--backend-info`, подсказка в
первом запуске «установите AppIndicator/KStatusNotifierItem Support»,
жест/CLI/уведомления работают (решение 2).

## 5. D4/D5 — уведомления и единственный экземпляр

`FdoNotifier` (`platform/qt`, DE-нейтрально): прямой
`org.freedesktop.Notifications.Notify`, `app_name=Traduku`, иконка
`accessories-dictionary`, `desktopFileName` выставлен; KDE сохраняет
`KdeNotifier` из-за именованных событий `traduku.notifyrc`.

`SingleInstance` (`platform/qt`): `QLocalServer` + владение сессионным
именем `dev.traduku.Traduku`; второй запуск передаёт первому команду
(`translate`, `gesture`) и выходит 0 — это же транспорт для D1-B. Заодно
чиним `--version` (не вызывается `setApplicationVersion`; пункт 1 из
«Найдено в текущей версии» в трекере).

---

## 6. Структура и сборка

```
src/app/cli.{h,cpp}            # общий QCommandLineParser (+--gesture, --backend-info, --no-tray)
src/app/gnome/main.cpp         # GNOME composition root
src/platform/evdev/            # EvdevHotkeyBackend            [D1]
src/platform/sni/              # SniTray, DBusMenuExporter     [D3]
src/platform/gnome/            # PortalGlobalShortcut, GsettingsGlobalShortcut, GestureService
src/platform/qt/               # + FdoNotifier, SingleInstance
src/platform/x11/              # + X11Clipboard                [D2]
tools/spike/                   # clip-spike.cpp, portal-spike.py, evdev-spike.cpp (не продукт)
tests/unit/, tests/integration/ # новые suites — см. этапы
```

`set(LT_SHELL "auto" CACHE STRING "kde|gnome|auto")` (`auto` = kde при
полных KF6, иначе gnome), `LT_TRAY=sni|kde`, `LT_BUILD_X11`, `LT_BUILD_EVDEV`.
FATAL_ERROR, если собираются два `traduku`-таргета. `.deb` — `-DLT_SHELL=gnome`
(иначе Depends уедут в KF6), Arch — `kde`; `--selftest` зелёный в обоих.
Новых зависимостей не появляется: только Qt6 + X11 (уже есть) + D-Bus из Qt.

---

## 7. Ветки: состояние и остаток

Схема одобрена и **выполнена частично**:

* создано `integration/gnome-base` = `pipeline` + merge `debian`
  (коммит `3fc9f6a`); конфликты разрешены объединением: один список VM
  грида (`arch-kde debian-kde debian-gnome ubuntu-kde ubuntu-gnome`,
  ssh 2201..2205, vnc :5901..), общий контейнер `traduku-pipeline-vmm`,
  control = Depends из `pipeline` + KF6-dev-пакеты из `debian`, changelog =
  запись `0.2.0-1`, install-deps = два шага (с GTK/appindicator gir и
  фолбэком), обе ветки логики типа сессии sddm.
* **Гейты перед мержем в `master` (не пройдены — на этом хосте нет Qt):**
  `ctest` 20/20; `TRADUKU_VM=arch-kde pytest tests/e2e` 8/8;
  `vm-functest.sh debian-kde` 7/7 и `ubuntu-gnome` 7/7. После `apt` из §8
  первый гейт запускается сразу, остальные — после `vmctl.sh up/mkvm`.
* После гейтов: `git switch master && git merge --no-ff integration/gnome-base`
  (ожидаемых конфликта два: `packaging/debian/changelog`,
  `src/platform/x11/X11HotkeyBackend.cpp` — берём версию с
  `RecordKeyEmitter`), затем уборка: `plan.md` и `pipeline_task.md` →
  `docs/archive/`, `--version`-фикс и `qt6-wayland` в PKGBUILD отдельными
  коммитами, тег, `archive/debian`/`archive/pipeline` и удаление веток.
* Дальнейшая GNOME-работа — `git switch -c gnome` от `master`.

---

## 8. Окружение этого хоста (команды; sudo нужен ваш пароль)

```bash
# сборка + e2e-тулинг гостя + инструменты ручной проверки
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-multimedia-dev qt6-5compat-dev \
  libx11-dev libxi-dev libxtst-dev libglib2.0-dev \
  wl-clipboard x11-utils xdotool xterm python3-pil python3-pytest
# (имена проверены apt-cache на этом релизе: Qt здесь 6.10.2,
#  qt6-5compat-dev есть, debian-имени libqt6core5compat6-dev нет)

# проверка, что всё ставится как ожидается (KDE-слой на GNOME не собираем)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLT_SHELL=gnome
cmake --build build && ctest --test-dir build --output-on-failure
./build/bin/traduku --backend-info

# жест через /dev/input: правило udev (НЕ группа input, см. D1)
sudo install -Dm644 packaging/udev/70-traduku-uaccess.rules \
     /etc/udev/rules.d/70-traduku-uaccess.rules
sudo udevadm control --reload
sudo udevadm trigger --action=add --subsystem-match=input   # add, не change
getfacl /dev/input/event3        # должно появиться user:user:rw-
./build/bin/evdev-spike listen 20
```

Грид (docker уже есть): `./tools/vms/setup-ctr.sh && ./tools/vms/vmctl.sh
up && ./tools/vms/vmctl.sh mkvm ubuntu-kde ubuntu kde && …`. Отдельный
долг: `mkvm.sh` ставит GNOME с `WaylandEnable=False` (X11-GNOME), а нам
нужен **GNOME Wayland** — см. Stage 7.

---

## 9. Этапы (каждый = зелёный `ctest` + коммит; «руками» = проверка у вас)

**Stage 0 — спайки и замеры. Частично готово (2026-11-04).**
Сделано и задокументировано в §0b: `tools/spike/clip-spike.cpp` (пути
чтения/записи буфера, A–E), `tools/spike/portal-spike.py` (идентичность для
портала, лестница из 5 попыток + запуск как настоящего приложения),
`tools/spike/evdev-spike.cpp` (инвентаризация `/dev/input`, слушатель
клавиш), `tools/spike/stage0.sh` (полевая прогонка «одной командой»),
подключение в CMake (`-DLT_BUILD_SPIKES`, по умолчанию ON, в ctest не
лезет). Решения, которые они дали: D2 = `WlClipboard` + `X11Clipboard`
(focus-трюк исключён), D1-B/C = портал заблокирован → gsettings пишем
сразу.
Осталось в Stage 0: (а) `evdev-spike listen` после установки правила
`packaging/udev/70-traduku-uaccess.rules` (uaccess, см. D1) — проверить,
что Ctrl+C+C детектируется и не ломает копирование; (б) тот же прогон на
гридной VM (QMP-клавиши должны прийти в `/dev/input`).

**Stage 1 — каркас GNOME-оболочки.** `app/cli.*` (вынести парсер из
`app/kde/main.cpp`, поведение KDE не меняется), `app/gnome/main.cpp`,
`LT_SHELL`, `--backend-info`, фикс `--version`. До этого шага на GNOME нет
ни одного бинаря (единственный executable — `app/kde`). *Руками:*
`traduku --translate --text "Привет, мир"` против `tools/mock-openai.py`,
`--selftest`, `--version`.

**Stage 2 — уведомления + единственный экземпляр (D4, D5).**
*Тесты:* фейковый сервер уведомлений на сессионной шине; второй процесс
будит первый. *Руками:* баннер GNOME при переводе и при ошибке endpoint.

**Stage 3 — жест (D1).** `EvdevHotkeyBackend` (юнит: `feedEvent` +
инжектируемый `DeviceFinder`, интервал из `Settings`, горячее подключение
устройства), `GestureService` + `GsettingsGlobalShortcut`
(инжектируемый `gsettings`-раннер), `PortalGlobalShortcut` (фейковый портал
на сессионной шине под инжектируемым именем). *Руками:* Ctrl+C+C на
Ubuntu GNOME; одиночное нажатие не срабатывает; ru-раскладка; без группы
`input` — понятное сообщение и работа через портал.

**Stage 4 — буфер (D2).** `X11Clipboard` + приоритеты в `ClipboardFactory`.
*Тесты:* разбор MIME/charset (общая таблица с `WlClipboard`), интеграционный
X-тест с `SKIP` без `DISPLAY`. *Руками:* копирование в gnome-text-editor →
жест → перевод; `CopyResult=true` → `wl-paste`/`xclip` показывает результат.

**Stage 5 — трей (D3) на GNOME.** `platform/sni`. *Тесты:* фейковый watcher
(`RegisterStatusNotifierItem`, `GetLayout`, `Event` по пункту меню →
`translateRequested`), отсутствие watchера → деградация. *Руками:* иконка в
панели Ubuntu, смена языка из меню.

**Stage 6 — автономность.** Первый запуск (приветствие, вопрос про
автозапуск, настройки), иконки Yaru/Adwaita, HiDPI, локаль ru, звук
PipeWire, перелогин → процесс жив.
Иконка-расширение на Ubuntu уже есть из пакета `gnome-shell-ubuntu-extensions`
(`ubuntu-appindicators@ubuntu.com`), в Debian это
`gnome-shell-extension-appindicator` — в `Recommends` оба имени.

**Stage 6b — `SniTray` в KDE (решение 4).** `LT_TRAY=sni` на arch-kde:
`test_04` + глаза (`vmctl.sh vnc arch-kde`). Только потом — коммит
«remove KdeTray» (с вашего «да»).

**Stage 7 — e2e-грид.** `tests/e2e/profiles.py` (KdeProfile/GnomeProfile:
sessionenv, анти-блокировка, тулзы буфера, ассерт окна, способ жеста),
новые тесты: `test_09_gesture_evdev` (Ctrl+C+C из QMP),
`test_10_gesture_portal`, `test_11_gesture_gsettings`,
`test_12_gnome_clipboard`, `test_13_no_tray_degrade`. `test_04` —
параметризация: KDE = watcher обязателен; GNOME = обязателен при
включённом расширении, иначе явный `SKIP` (не vacuous). Инфраструктура:
`mkvm.sh` должен уметь **GNOME Wayland** (сейчас `WaylandEnable=False`) и
образ Ubuntu 26.04; `mkarchbase.sh` — кандидат на `arch-gnome` позже.
Прогоны — последовательно (урок про два грида).

**Stage 8 — упаковка и документация.** `-DLT_SHELL=gnome` в
`packaging/debian/rules`, Depends без KF6, `Recommends:
gnome-shell-extension-appindicator | gnome-shell-ubuntu-extensions,
wl-clipboard`, `Suggests: xdg-desktop-portal-gnome`; README: таблица «что работает на каком DE/сессии», раздел про
группу `input` и приватность evdev, раздел про расширение; ARCHITECTURE.md:
раздел GNOME вместо TODO; версия 0.3.0 в трёх местах (`CMakeLists.txt`,
`PKGBUILD`, `changelog`).

---

## 10. Открытые вопросы

1. evdev = доступ к `/dev/input` через uaccess-правило (умение читать
   клавиатуру активным пользователем сессии). Ок как рекомендуемый путь с
   явным предупреждением, или делать его опцией конфига по умолчанию
   `false`?
2. Если спайк покажет, что фоновое чтение буфера на муттере невозможно, а
   XWayland-мост работает — устраивает ли зависимость GNOME-версии от
   XWayland (на системах с выключенным XWayland буфер не читается)?
3. Порядок этапов: трогать ли `KdeTray`/KGA до того, как GNOME-версия
   полностью готова (Stage 6b запланирован после Stage 5)?

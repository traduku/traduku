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
| `/dev/input/event*` | `crw-rw---- root:input`; группы `input` у пользователя **нет** | evdev-жест требует `usermod -aG input` (или ACL) — см. D1 |
| `org.gnome.SettingsDaemon.MediaKeys` | есть, `custom-keybindings` пуст | gsettings-fallback возможен |
| `wl-clipboard`, `xclip` | не установлены | Stage 0 их ставит: без них `WlClipboard` недоступен |
| Qt6/KF6 dev | не установлены | §8 |
| qemu на хосте | нет; docker есть; `~/vms` пуст (мы в Ubuntu, грид жил на Arch-стороне) | грид поднимается заново: `vmctl.sh up` + `mkvm` |

Неубедительно и **не** принято как факт: попытки прочитать буфер из фонового
GTK3-клиента (`request_text`, `wait_for_text`, несколько вариантов, включая
клиент, который сам только что записал текст) вернули `None` — но и
«фокусный» GTK-клиент вернул `None`, то есть подозрение на сам GTK-спайк, а
не на муттер. Вопрос «можно ли читать буфер на GNOME Wayland без фокуса»
закрывается Qt-спайком Stage 0 (§9), а не догадками: от него зависит, какой
из путей D2 становится основным.

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

* Цена: пользователю нужна группа `input` (`sudo usermod -aG input $USER`,
  как у `input-remapper`/`keyd`) — это «может читать все клавиатуры»
  разрешение, оно **оп-ин** и обязано быть описано в README и в `--help`.
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

**Кандидат 3 (без спецправ, но не Ctrl+C+C): `PortalGlobalShortcut`.**
`CreateSession` → `BindShortcuts([{id:"translate", description,
preferred_trigger:"<Alt>Ctrl+c"}], parent_window, {})` → диалог
согласования → `Activated`/`Deactivated` → `ShortcutMonitor` →
`DoublePressDetector`. Реализация на `QDBusInterface`, имя сервиса и путь
**инжектируются** (как `ToolFinder` в `WlClipboard`) — иначе тест не
подделать. `ListShortcuts()` при старте, чтобы не показывать диалог
повторно. Спайк Stage 0 обязан ответить: пускает ли gnome-impl не-sandboxed
приложение, нужен ли установленный `traduku.desktop`, что в `options`
(`activation_token`?), какая `version` интерфейса.

**Кандидат 4 (последний): `GsettingsGlobalShortcut`.** Пишет
`org.gnome.settings-daemon.plugins.media-keys custom-keybindings` +
`…/custom0/{name,command,binding}`, `command=gdbus call --session -d
dev.traduku.Traduku -o /dev/traduku/Traduku -m
dev.traduku.Traduku.gesturePressed`; каждое нажатие — вызов метода
работающему экземпляру, пары режет тот же детектор. Раннер `gsettings`
инжектируется, `unregister()` убирает binding. Работает и на Debian 13, и
на Ubuntu 22.04. **Правило из решения 3:** пишем сразу, если спайк Stage 0
показал портал нерабочим (нет диалога / `Invalid session` / требует
sandboxed app_id / impl отсутствует в нужной версии); иначе — вместе с
Debian 13 в матрице.

**Кандидат 5 (опция, later):** расширение GNOME Shell, наблюдающее клавиши
и отдающее буфер по D-Bus. Даёт Ctrl+C+C без группы `input` и решает D2, но
это отдельная поддержка и `gnome-extensions install` + перезаход в сессию.
Флаг `LT_EXPERIMENT_SHELL_EXT`; возвращаемся, если evdev или D2 не устроят.

---

## 3. D2 — буфер на GNOME Wayland (риск №1)

Порядок, от дешёвого к дорогому; победителя определяет спайк Stage 0.

1. **Спайк (`tools/spike/clip-spike.cpp`).** Четыре способа замера, каждый
   дважды (после Ctrl+C в gnome-text-editor и после `wl-copy`): фоновый
   `QClipboard::text()`; то же после `show()+requestActivate()`; `wl-paste`
   как дочерний процесс; X11-соединение к XWayland (`XConvertSelection`).
   Критерий: непустой текст ≤200 мс.
2. **`X11Clipboard` (`platform/x11`) — ожидаемый победитель.** Муттер
   зеркалит wayland-буфер в XWayland, а X11-семантика не требует фокуса:
   `XGetSelectionOwner`/`XConvertSelection` для чтения и
   `XSetSelectionOwner` для записи. Этим же путём уже пользуются e2e-гейты
   ветки `debian` (`xclip` на ubuntu-gnome). Подключение — к `:0` с
   `XAUTHORITY` из `/proc/<Xwayland-pid>/environ` (тот же приём, что у
   `capture_sessionenv`). Авторизацию и пути инжектируем; юнит-тесты — на
   слое разбора MIME/charset (общий с `WlClipboard`), интеграционный — под
   реальным X, с `SKIP` без `DISPLAY`.
3. **Focus-window трюк** (если XWayland недоступен): жест показывает окно,
   чтение откладывается до `QClipboard::dataChanged` с таймаутом 300 мс;
   при таймауте — честное уведомление «нет доступа к буферу».
4. **Расширение Shell** (кандидат 5 из D1) — если 2 и 3 не сработали.
5. `WlClipboard` (wl-clipboard) остаётся для композиторов с
   `ext-data-control` (KWin, Hyprland); на муттере он не помогает — муттер
   протокол не реализует.

Запись результата (`CopyResult`) идёт тем же каналом, что и чтение; на
Wayland-клиенте — через активированное окно (`QClipboard::setText`).

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

# жест через /dev/input (решение 1): группа input + перелогин
sudo usermod -aG input $USER   # затем выйти/войти; проверка: getent group input
```

Грид (docker уже есть): `./tools/vms/setup-ctr.sh && ./tools/vms/vmctl.sh
up && ./tools/vms/vmctl.sh mkvm ubuntu-kde ubuntu kde && …`. Отдельный
долг: `mkvm.sh` ставит GNOME с `WaylandEnable=False` (X11-GNOME), а нам
нужен **GNOME Wayland** — см. Stage 7.

---

## 9. Этапы (каждый = зелёный `ctest` + коммит; «руками» = проверка у вас)

**Stage 0 — спайки и замеры (без продуктового кода).**
`tools/spike/clip-spike.cpp` (D2.1), `tools/spike/portal-spike.py`
(GlobalShortcuts: диалог? `Activated`/`Deactivated`, `timestamp`, нужна ли
sandboxed-идентичность), `tools/spike/evdev-spike.cpp` (Ctrl+C+C из
`/dev/input` после выдачи группы). Результат — таблица в этом файле и
решения: победитель D2, «писать ли gsettings сразу» (правило D1-B).
*Документировать в §0/§9.*

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

1. evdev = группа `input` (умение читать все клавиатуры). Ок как
   рекомендуемый путь с явным предупреждением, или делать его опцией
   конфига по умолчанию `false`?
2. Если спайк покажет, что фоновое чтение буфера на муттере невозможно, а
   XWayland-мост работает — устраивает ли зависимость GNOME-версии от
   XWayland (на системах с выключенным XWayland буфер не читается)?
3. Порядок этапов: трогать ли `KdeTray`/KGA до того, как GNOME-версия
   полностью готова (Stage 6b запланирован после Stage 5)?

# Traduku — план GNOME-версии

Цель: второй composition root (`app/gnome`) + слой `platform/gnome`, чтобы
`traduku` работал на GNOME Wayland/X11 **без KF6**, с треем, уведомлениями,
глобальным жестом и переводом буфера обмена. Ядро (`core/`), UI (`ui/`) и
`platform/qt|wl` не трогаем — они DE-нейтральные по построению
(ARCHITECTURE.md). Правило TDD из ARCHITECTURE.md действует: каждый кусок
продуктового кода начинается с падающего теста.

Хост для ручной проверки: **Ubuntu 26.04 LTS, GNOME Shell 50.1, сессия
Wayland** (замеры — ниже).

---

## 0. Замеры на реальном хосте (2026-11-04, Ubuntu 26.04 / GNOME 50.1 / Wayland)

Надёжные (проверено напрямую, `busctl`/`gdbus`/`dpkg`/`gsettings`):

| Что | Факт | Значение для плана |
|---|---|---|
| `org.kde.StatusNotifierWatcher` | **есть**, `IsStatusNotifierHostRegistered=true` (расширение `ubuntu-appindicators@ubuntu.com`, включено) | трей на GNOME возможен без своего расширения — но только когда расширение установлено |
| реестр SNI | сейчас заняты только ayatana-иконки apt/unattended-upgrades | наш предмет — `org.kde.StatusNotifierItem` + `com.canonical.dbusmenu`; расширение официально поддерживает и то и другое |
| `org.freedesktop.Notifications` | есть (сервер GNOME) | `INotifier` = прямой D-Bus, libnotify не нужен |
| `org.freedesktop.portal.GlobalShortcuts` | **есть** (front-door + `org.freedesktop.impl.portal.GlobalShortcuts` у `xdg-desktop-portal-gnome` 50.0) | штатный Wayland-механизм глобального жеста; `Activated`/`Deactivated` = press/release → кормим существующий `DoublePressDetector` |
| `org.freedesktop.portal.Clipboard` | есть, но по спецификации **только над sessions RemoteDesktop/InputCapture** | для чтения буфера обычным приложением **не годится** (InputCapture = перехват всей клавиатуры, красное предупреждение муттера) |
| `org.gnome.Mutter.Clipboard` | **нет** на шине | готового «прочитай буфер из фона» API в GNOME 50 нет |
| `org.gnome.SettingsDaemon.MediaKeys` | есть, `custom-keybindings` пуст | запасной механизм жеста через gsettings |
| `wl-clipboard`, `xclip` | **не установлены** | `WlClipboard` сейчас недоступен → путь только через Qt/X11 |
| Qt6/KF6 dev-пакеты | не установлены (`dpkg -l | grep -c libqt6` = 0, `/usr/lib/cmake/Qt6` нет) | Stage 0 начинается с build-deps |
| qemu на хосте | нет; docker есть | e2e-грид живёт в контейнере, как в `pipeline` |

Неубедительно (требует Qt-спайка, GTK3-измерения тут не инструмент):
попытка прочитать буфер из фонового Wayland-клиента (`Gtk.Clipboard
request_text`/`wait_for_text`, несколько вариантов, включая клиент, который
сам только что записал текст в буфер) вернула `None`. Но и «фокусный» клиент
с окном прочитал `None`, поэтому вывод «муттер режет чтение без фокуса»
считать **недоказанным** — это риск №1, который закрывается спайком Stage 0
на настоящем Qt-двоичном файле, а не на GTK-обёртке.

Почему это важно: если фонового чтения нет, то `QClipboard::text()` по
жесту вернёт пусто, и нужен один из обходных путей (D2).

---

## 1. Что уже готово и что отсутствует (по интерфейсам)

| Интерфейс | KDE | GNOME | Комментарий |
|---|---|---|---|
| `IClipboard` | Qt/Wl через `ClipboardFactory` | **решение D2** | на X11-сессии GNOME `QtClipboard` работает как есть |
| `IHotkeyBackend` | `X11HotkeyBackend` (X11/XWayland) | тот же на X11-сессии; на Wayland — **решение D1** | пассивное наблюдение Ctrl+C на Wayland запрещено в принципе |
| `IGlobalShortcut` | `KdeGlobalShortcut` (KGlobalAccel) | **`PortalGlobalShortcut` + `GsettingsGlobalShortcut`** (D1) | KF6 тут быть не должно |
| `INotifier` | `KdeNotifier` (KNotification + notifyrc) | **`FdoNotifier`** (D4) | звуки и так работают: `core/SoundPlayer` |
| `IAutostart` | `XdgAutostart` | он же | GNOME уважает XDG autostart — проверять нечего |
| трей | `KdeTray` (KF6 `KStatusNotifierItem`) | **`SniTray`** (D3), чистый D-Bus | тот же контракт сигналов: `translateRequested/settingsRequested/targetLanguagePicked` |
| единственный экземпляр | `KDBusService(Unique)` | **`SingleInstance`** (D5) | KF6-зависимость, переписываем на QLocalServer/D-Bus |
| composition root | `app/kde/main.cpp` | **`app/gnome/main.cpp`** | копипаст только аргументов CLI: вынести парсер в `app/cli.*` |
| идентификатор приложения | — | `QGuiApplication::setDesktopFileName("traduku")` + установленный `traduku.desktop` | нужен порталу, уведомлениям и иконке окна в GNOME |

---

## 2. Пять решений (D1…D5)

### D1. Глобальный жест на GNOME

Продуктовая правда, которую надо принять вслух: **настоящий Ctrl+C+C на
GNOME Wayland ненаблюдаем**. Ни XInput2 (KWin-а нет, муттер не отдаёт
native-клавиши в XWayland), ни grab (сломал бы копирование). Поэтому:

- **A (рекомендую).** `PortalGlobalShortcut` — `org.freedesktop.portal.GlobalShortcuts`:
  `CreateSession` → `BindShortcuts([{id:"translate", description,
  preferred_trigger:"<Alt>Ctrl+c"}], parent_window, {})` → диалог
  подтверждения у пользователя → сигналы `Activated`/`Deactivated`
  (press/release!) → тот же `ShortcutMonitor` + `DoublePressDetector`.
  Реализация на `QDBusInterface` (libportal не тянем), имя сервиса и путь
  **инжектируются** (как `ToolFinder` в `WlClipboard`) — иначе тест не
  подделать. Плюс `ListShortcuts()` при старте, чтобы не показывать диалог
  повторно, и `ConfigureShortcuts()` в меню «Settings…» (portal v2).
  Требует xdg-desktop-portal-gnome ≳ 48 (Ubuntu 26.04: 50.0 ✓; Debian 13:
  ✗ → fallback B).
- **B (fallback, обязан быть).** `GsettingsGlobalShortcut`: пишем
  `org.gnome.settings-daemon.plugins.media-keys custom-keybindings` +
  `…/custom-keybindings/custom0/{name,command,binding}`, где
  `command=gdbus call --session -d dev.traduku.Traduku -o /dev/traduku/Traduku
  -m dev.traduku.Traduku.gesturePressed`. Каждое нажатие — вызов метода
  работающему экземпляру; дубликаты гасит тот же детектор. Готовится через
  инъекцию раннера (`gsettings` CLI), чистится при `unregister()`. Работает
  и на Debian 13, и на Ubuntu 22.04.
- **C (отложено).** Сопутствующее расширение GNOME Shell (наблюдение
  клавиш + чтение буфера через `meta.Clipper`). Даёт настоящий Ctrl+C+C, но
  ломается на каждой мажорной версии Shell и требует
  `gnome-extensions install`. Завести под флагом `LT_EXPERIMENT_SHELL_EXT`,
  вернуться к нему, если A+B разочаруют.
- **D.** На X11-сессии GNOME ничего не меняем: `X11HotkeyBackend` (в версии
  из ветки `debian` — с вынесенным хелпер-процессом RECORD) даёт
  пассивный Ctrl+C+C как в KDE-X11.

### D2. Чтение буфера на GNOME Wayland (риск №1)

Порядок такой, первым — самый дешёвый и беззависимостный:

1. **Спайк Stage 0.** Минимальный Qt-хелпер (`tools/spike/clip-spike.cpp`):
   фоновый `QClipboard::text()`, то же после `show()+requestActivate()`,
   то же через `wl-paste`, то же из X11-соединения. Критерий: что реально
   отдаёт непустой текст за ≤200 мс после Ctrl+C в gedit. Решение принимаем
   по таблице, а не по догадкам.
2. **Путь XWayland (ожидаемый победитель).** Отдельное X11-соединение
   (`xcb`) читает `CLIPBOARD` через `XConvertSelection` без фокуса — на
   X11-семантике фокус не нужен, а муттер зеркалит wayland-буфер в
   XWayland. Этим же путём уже пользуется ветка `debian` в e2e-гейте
   (`xclip` на ubuntu-gnome). Реализуется как `X11Clipboard` в
   `platform/x11` (тот же дисциплинарный паттерн: соединения/раннеры
   инжектируются) и добавляется в `ClipboardFactory` приоритетом после
   `WlClipboard`. Ограничение: нет XWayland → следующий пункт.
3. **Focus-window трюк.** Жест сам вызывает `window.show()+requestActivate()`,
   чтение буфера откладывается до прихода `data_offer`
   (`QClipboard::dataChanged`), с таймаутом 300 мс и уведомлением «нет
   доступа к буферу». Побочный эффект — окно всплывает само, что для
   переводчика нормально.
4. **Расширение Shell (вариант C из D1)** — если 2 и 3 не сработали.

### D3. Трея

`src/platform/sni/` — клиент StatusNotifierItem на чистом Qt/D-Bus:
регистрация у `org.kde.StatusNotifierWatcher`, интерфейс
`org.kde.StatusNotifierItem` (Category/Id/Title/Status/IconName/ToolTip/
ItemIsMenu + `Activate`/`ContextMenu`/`SecondaryActivate`) и
`com.canonical.dbusmenu` для меню (`GetLayout`, `GetGroupProperties`,
`Event`, `AboutToShow`). Сигналы те же, что у `KdeTray`, поэтому
`AppController` не меняется. Если watchera нет (чистый GNOME без
расширения) — логируем `tray=none (no StatusNotifierWatcher)` и живём без
иконки: жест, CLI и уведомления работают. `KdeTray` сейчас **не**
переписываем (потом можно унифицировать).

### D4. Уведомления

`FdoNotifier` в `platform/qt` (DE-нейтрально): прямой
`org.freedesktop.Notifications.Notify` с `app_name="Traduku"`,
`desktopFileName` выставлен, иконка `accessories-dictionary`. KDE оставляет
себе `KdeNotifier` из-за именованных событий в `traduku.notifyrc`.
Тестируется фейковым сервером на сессионной шине (инъекция имени сервиса).

### D5. Один экземпляр и «второй запуск = переведи»

`SingleInstance` в `platform/qt`: `QLocalServer` на имени
`traduku-$UID` + владение сессионным именем `dev.traduku.Traduku`. Второй
запуск шлёт первому команду (`translate` / `gesture`) и выходит 0. Заодно
это транспорт для D1-B. Плюс чиним давно найденное: `--version` печатает
пустоту, потому что `QApplication::setApplicationVersion` не вызывается
(запись в `pipeline_task.md`, п.1).

---

## 3. Структура и переключатели сборки

```
src/app/cli.{h,cpp}            # вынесенный QCommandLineParser (+--gesture, --backend-info, --no-tray)
src/app/gnome/main.cpp         # GNOME composition root
src/platform/sni/              # SniTray (Qt-only SNI + dbusmenu)
src/platform/gnome/            # PortalGlobalShortcut, GsettingsGlobalShortcut,
                               # GestureService (dev.traduku.Traduku), GnomeSessionInfo
src/platform/qt/               # + FdoNotifier, SingleInstance
src/platform/x11/              # + X11Clipboard (XConvertSelection без фокуса)  [D2]
tests/unit/                    # + tst_snitray, tst_fdonotifier, tst_singleinstance,
                               #   tst_portalglobalshortcut, tst_gsettingsglobalshortcut
tests/integration/             # + tst_gestureservice, tst_clipboards (X11Clipboard),
                               #   tst_gnomeshell_wiring
```

CMake: вместо булевых пар — один кэш-переключатель оболочки
`set(LT_SHELL "auto" CACHE STRING "kde|gnome|auto")` (`auto` = kde, если
найдены все KF6, иначе gnome), плюс `LT_BUILD_SNI`, `LT_BUILD_X11` как есть.
Правило: `add_subdirectory(app/gnome)` только когда `LT_SHELL=gnome`;
FATAL_ERROR, если одновременно два `traduku`-таргета. Дебаевский/убунтовый
пакет собирается с `-DLT_SHELL=gnome` (иначе в Depends полезут KF6),
Arch-пакет — `kde`; `--selftest` обязан проходить в обоих.

`--backend-info` (текстом: clipboard=?, hotkey=?, notifier=?, tray=?,
shortcut=?, session=wayland/x11) — одна строка на бэкенд. Это то, чем
_manually_ проверяется конфигурация на чужой машине и что парсят e2e-тесты.

---

## 4. Этапы (каждый = зелёный `ctest` + коммит)

**Stage 0 — окружение и спайки (без продуктового кода).**
`sudo apt-get install -y qt6-base-dev qt6-multimedia-dev
qt6-5compat-dev libx11-dev libxi-dev libxtst-dev wayland-protocols
libkf6*‑dev pkg-config ninja-build cmake` (список = `packaging/debian/control`
из `debian`), сборка `-DLT_SHELL=auto` (на этом хосте → `gnome`, т.е. пока
без исполняемого файла — это и есть мотивация Stage 1), затем
`tools/spike/clip-spike.cpp` с четырьмя способами чтения буфера (D2.1) и
`tools/spike/portal-spike.py` (CreateSession+BindShortcuts против живого
портала: какой диалог, что в `Activated`, `Deactivated`, `timestamp`).
Документировать результаты таблицей в этом файле. Выход: понятен победитель
D2 и подтверждён D1-A.

**Stage 1 — каркас GNOME-оболочки.** `app/cli.*` (вынести парсер из
`app/kde/main.cpp`, поведение KDE не меняется — 20/20 ctest как proof),
`app/gnome/main.cpp`, `LT_SHELL`, `--backend-info`, фикс `--version`.
До этого шага на GNOME нет вообще никакого бинаря (единственный
executable — `app/kde`), поэтому Stage 1 уже приносит `--selftest`,
`--translate` и `--text` на GNOME. Ручная проверка: `traduku --translate
--text "Привет мир"` против `tools/mock-openai.py`.

**Stage 2 — уведомления + единственный экземпляр (D4, D5).**
`FdoNotifier` (тест: фейковый сервер уведомлений), `SingleInstance`
(тест: второй процесс выходит 0 и «будит» первый). Ручная проверка: баннер
GNOME при жесте и при ошибке endpoint.

**Stage 3 — жест (D1).** `GestureService` (D-Bus `dev.traduku.Traduku`),
`PortalGlobalShortcut` + `GsettingsGlobalShortcut`, выбор между ними по
наличию интерфейса на шине; интеграция с `ShortcutMonitor`. Тесты: фейковый
портал на сессионной шине (инжектируемое имя сервиса), фейковый `gsettings`
раннер, `--gesture` из CLI. Ручная проверка: диалог портала, двойное
нажатие, одиночное не срабатывает, ru-раскладка.

**Stage 4 — буфер (D2).** `X11Clipboard` + приоритеты в `ClipboardFactory`
(тесты на `feedSelection`-подобном инъекционном API + интеграционный тест
против реального X-сервера помечать `SKIP` без `DISPLAY`). Ручная
проверка: копирование в gnome-text-editor → жест → текст в окне.

**Stage 5 — трей (D3).** `SniTray` + `platform/sni`; тесты: фейковый
watcher, проверка `RegisterStatusNotifierItem`, `GetLayout` меню, `Event`
по пункту «Translate Clipboard Now» → `translateRequested`, отсутствие
watchera → деградация без падения. Ручная проверка: иконка в верхней панели
Ubuntu, меню, переключение языка.

**Stage 6 — автономность.** Первый запуск на GNOME (приветствие, вопрос про
автозапуск, открытие настроек), иконки/темы (`Yaru` vs `Adwaita`),
HiDPI-скриншот, локаль ru, звук через PipeWire. Проверка `autostart`:
перелогин → процесс жив.

**Stage 7 — e2e-грид (см. §5).**

**Stage 8 — упаковка и документация.** `-DLT_SHELL=gnome` в
`packaging/debian/rules`, Depends без KF6 (+`gsettings-desktop-schemas`,
`xdg-desktop-portal-gnome` как Recommends), README: таблица «что работает
на каком DE/сессии» и честная строка про Wayland-жест; ARCHITECTURE.md:
раздел GNOME вместо TODO; версия 0.3.0 в `CMakeLists.txt`,
`packaging/arch/PKGBUILD`, `packaging/debian/changelog`.

---

## 5. Тестовая матрица GNOME (поверх `pipeline`)

Инфраструктура `pipeline` (`tools/vms/`, `tests/e2e/`) переиспользуется
целиком; добавляется профиль DE, чтобы не переписывать тесты 1–8
(правило «не менять существующие тесты без согласования»):

```
tests/e2e/profiles.py     # KdeProfile / GnomeProfile: sessionenv, анти-блокировка
                          # экрана, tools буфера (wl-copy/xclip), ассерт окна
                          # (KWin scripting vs xdotool/Xwayland vs journal), жест (KGA vs portal/gsettings)
tests/e2e/test_04_tray    # → параметризовать: KDE = watcher обязателен,
                          #    GNOME = обязателен при включённом расширении,
                          #    иначе SKIP с явной причиной (не vacuous!)
tests/e2e/test_09_gesture_portal   # BindShortcuts через портал → QMP-пара → перевод
tests/e2e/test_10_gesture_gsettings# custom-keybinding → gdbus gesturePressed → перевод
tests/e2e/test_11_gnome_clipboard  # seed → жест → результат в буфере (X11-путь и wayland-путь)
tests/e2e/test_12_no_tray_degrade  # выключенное расширение: приложение живо, CLI работает
```

VM: `ubuntu-gnome` (Wayland, образ Ubuntu 26.04 — тот же релиз, что и
ручная машина) и `debian-gnome` (X11, уже есть в ветке `debian`, гейт
`vm-functest.sh` 7/7). `mkvm.sh` объединяет ветки (см. §6) и принимает
`<distro> <de>` из множества {arch,debian,ubuntu,fedora} × {kde,gnome}.
Порядок прогонов — последовательный (урок из `pipeline_task.md`: два
грида одновременно = тайминг-флейки).

---

## 6. Что делать с ветками `debian` и `pipeline`

**Слить — да, и до начала GNOME-работы.** Это не две параллельные фичи:
`pipeline` ответвился от `debian` на коммите `2684afa` (содержимое
эквивалентно `master`), и с тех пор `debian` ушла на 7 коммитов вперёд
(X11 RECORD-хелпер, Build/Runtime-Depends, гейт `vm-functest.sh` на 4 VM),
а `pipeline` — на 22 (QEMU-грид, pytest e2e 1–8, `.deb`-пайплайн,
`mkrelease --sign`). Один и тот же чин (flaky commit-order, регистр HTTP-
заголовков) сделан дважды двумя cherry-pick'ами — классический признак
расхождения. GNOME-работа опирается и на e2e-инфраструктуру (`pipeline`), и
на X11-хелпер (`debian`), так что third-branch поверх master дала бы
трёхсторонние конфликты в `tools/vms/` — а они уже сейчас add/add:
`Dockerfile`, `mkvm.sh`, `qmp.py`, `vm-install.sh`, `vmctl.sh`, `vmlife.sh`
(plus `packaging/debian/control`).

Порядок слияния (от менее спорного к master):

```bash
git switch pipeline && git switch -c integration/gnome-base   # рабочая ветка слияния
git merge --no-ff debian          # конфликты: tools/vms/* (union: arch-логика pipeline
                                  # + дистрибутив/DE-логика debian), packaging/debian/control
                                  # (взять pipeline-версию + libkf6coreaddons-dev, libkf6i18n-dev),
                                  # changelog (взять debian: запись 0.2.0-1)
# дубли-патчи тестов (7d37663/d28930e, 7f82696/ae9295b) оставить один раз
ctest --test-dir build            # 20/20
TRADUKU_VM=arch-kde  pytest tests/e2e      # 8/8
./tools/vms/vm-functest.sh debian-kde      # 7/7 (валидирует X11 RECORD-хелпер)
./tools/vms/vm-functest.sh ubuntu-gnome    # 7/7 (здесь же первая проверка GNOME-сессии)

git switch master && git merge --no-ff integration/gnome-base
# ожидаемых конфликта два: packaging/debian/changelog и
# src/platform/x11/X11HotkeyBackend.cpp (взять debian-версию с RecordKeyEmitter)
ctest --test-dir build && TRADUKU_VM=arch-kde pytest tests/e2e
```

Затем один «уборочный» коммит: `plan.md` (debian) и `pipeline_task.md`
(pipeline) — трекеры сессий, у них нет дома; перенести в `docs/archive/`
или удалить по вашему слову; `--version`-фикс и `qt6-wayland` в depends
PKGBUILD (пункты 1–2 из «Найдено в текущей версии») — отдельными
маленькими коммитами с тестами. После — тег `v0.2.1`, ветки `debian` и
`pipeline` пометить тегами `archive/debian`, `archive/pipeline` и удалить
(история остаётся в мерж-коммитах).

Альтернатива «не сливать сейчас» стоит того только если вы хотите держать
e2e-инфраструктуру отдельно от продуктового дерева; тогда GNOME-ветку всё
равно надо начинать от `pipeline`, а 4 уникальных коммита `debian`
(в первую очередь X11-хелпер) при этом чerry-pick'аются вручную — т.е.
та же работа, только с повторным разрешением конфликтов потом. Не советую.

---

## 7. Открытые вопросы (нужен ваш ответ)

1. **Жест на Wayland.** Принимаем, что на GNOME Wayland жестом становится
   зарегистрированная пара (`Ctrl+Alt+C` ×2 через портал/gsettings), а
   Ctrl+C+C остаётся только на X11-сессиях? Или сразу закладываем
   расширение Shell (D1-C)?
2. **Трея на чистом GNOME** (без `gnome-shell-extension-appindicator`)
   считаем приемлемой деградацией без иконки?
3. **`gsettings`-fallback (D1-B)** писать сразу вместе с порталом или
   дождаться Debian 13 в матрице?
4. **`KdeTray`** — оставляем как есть (рекомендую) или сразу мигрируем KDE
   на общий `SniTray` (дешевле сопровождение, дороже риск регрессий)?
5. **Ветки** — схема §6 ок? Куда определить `plan.md`/`pipeline_task.md`:
   `docs/archive/` или удалить?
6. **Можно ли ставить build-deps на эту машину** (`apt-get install` из
   Stage 0) и поднимать grid-VM (docker, ~4 vCPU) — или сборку держим в
   distrobox?

---

## 8. Что можно потрогать руками уже сегодня (после Stage 0–1)

```bash
sudo apt-get install -y qt6-base-dev qt6-multimedia-dev qt6-5compat-dev \
  libx11-dev libxi-dev libxtst-dev pkg-config cmake ninja-build
cmake -S . -B build -DLT_SHELL=gnome -G Ninja && cmake --build build
./build/bin/traduku --backend-info
python3 tools/mock-openai.py 8931 &
./build/bin/traduku --translate --text "Привет, мир"
```

Дальше по этапам: Stage 2 даёт баннеры GNOME, Stage 3 — жест с диалогом
портала, Stage 5 — иконку в верхней панели Ubuntu.

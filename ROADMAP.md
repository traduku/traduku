# Traduku — Roadmap

Единый план вместо `plan.md`, `pipeline_task.md`, `PIPELINE.md`,
`GNOME_PLAN.md` и `TODO.md`. Архитектура — в [ARCHITECTURE.md](ARCHITECTURE.md),
установка — в [README.md](README.md). Ход мыслей и тупики — в `git log`,
здесь только то, что определяет следующие коммиты.

Правило из ARCHITECTURE.md действует на каждую строку этого файла:
кусок продукта без упавшего сначала теста не принимается.

---

## 1. Обещание

**Двойное Ctrl+C переводит буфер** — на KDE Plasma и на GNOME, на Wayland и
на X11. Буфер читается, результат пишется обратно, есть трей, уведомления,
звук и автозапуск. Ядро (`core/`) и UI (`ui/`) не знают про DE.

---

## 2. Закрытые решения (с доказательством, не с мнением)

| Решение | Почему | Где проверено |
|---|---|---|
| **Жест = evdev** (`/dev/input/event*`, без `EVIOCGRAB`) на Wayland; XI2/RECORD — на X11 | композитор не отдаёт клавиши клиентам; evdev — единственное, что даёт именно Ctrl+C+C, и не перехватывает копирование | `evdev-spike` + живое Ctrl+C+C на GNOME 50.1; e2e 05/07/08 на `ubuntu-gnome` |
| **Портал GlobalShortcuts — не делаем** | `CreateSession` → `portal.Error.NotAllowed: An app id is required` во всех 5 попытках, включая запуск через `gio launch`: x-d-p 1.21 выдаёт app-id только sandboxed | `tools/spike/portal-spike.py` |
| **Буфер = `wl-clipboard`** (hard-Depends в GNOME-пакете), `X11Clipboard` — запас, Qt — только X11 | фоновое `QClipboard` на муттере пусто (251 мс), `wl-paste` 13 мс, `XConvertSelection` 10 мс | `tools/spike/clip-spike.cpp` |
| **Focus-трюк исключён** | `show()+requestActivate()` не даёт фокуса фоновому окну (замер B) — только кража фокуса | там же |
| **Трей = `QSystemTrayIcon`**, `KdeTray` удалён | Qt's generic unix SNI-клиент работает и на Plasma, и на GNOME; `KStatusNotifierItem` падает при деструкте с живой шиной (coredump), а две реализации = расхождение по десктопам | e2e `test_04` на arch-kde, глазами на Plasma и GNOME, `tst_trayparity` |
| **Без tray-host приложение работает** | жёст, буфер и FDO-уведомления не зависят от иконки; `Suggests:` на расширение — чтобы не предлагать shell-расширение тому, кто в трей не заглядывает | `ubuntu-gnome` без расширения: 3 зелёных теста |
| **Подсказка о клике живёт в меню** | хост GNOME не читает SNI-свойство `ToolTip` (в `appIndicator.js` ни одного обращения) | `--backend-info` + `tst_trayparity` |
| **Доступ к клавиатуре = udev `uaccess`, не группа `input`** | группа даёт чтение клавиатуры всем программам аккаунта; uaccess выдаёт ACL активной seat-сессии | `getfacl` на хосте и в госте |
| **Имя правила — `70-*` и триггер `--action=add`** | `99-*` вешает тег после `71-seat.rules` — ACL не появляется, хотя `udevadm info` показывает `:uaccess:` | `packaging/udev/70-traduku-uaccess.rules` |
| **пакеты сами применяют правило** | файл правила не меняет ACL уже существующих устройств → «поставь и перезагрузись» для программы, у которой единственный интерфейс — клавиши | postinst / `.install`-хук, проверено в госте |
| **Два флейвора из одного дерева**: `traduku` (KF6) и `traduku-gnome` (без KF6) | `LT_SHELL=auto` брал KDE, стоило KF6 оказаться в системе; GNOME-слой обязан быть KF6-свободным — это проверяется `ldd` | `test_01` на arch-kde и ubuntu-gnome |
| **Один CLI на обе оболочки** (`app/cli.cpp`) | KDE-`main.cpp` годами держал свой парсер: `--version` без номера, `--backend-info` = «Unknown option», своя копия one-shot с другим текстом ошибки | `tst_cli`, `--backend-info` на Plasma |
| **Гости ничего не компилируют** | доставка — пакеты; компиляция в госте проверяла хост и маскировала отсутствующие runtime-Depends | `vm-install.sh`, `check-grid.sh` |
| **Доказательство окна на Wayland — строка самого приложения** | клиент Wayland не вправе перечислять окна чужих клиентов, а у GNOME Shell нет KWin-скриптинга: список окон спрашиваем у KWin (Plasma) и у `xdotool` (X11), на Wayland читаем, что приложение сказало о своём окне, а QMP-диф остаётся перекрёстной проверкой, которую один источник не подделает | `grid.window_evidence` + `tst_appcontroller::gestureLogsTheWindowItMapped` (лог обязан совпадать с реальным окном) |
| **Идентичность предмета в трее — по `ToolTip`, не по `Title`** | `Title`/`Id` Qt выводит из собственного application name — деталь версии Qt, а не продукта: один и тот же источник на Plasma даёт `Traduku`, на Ubuntu (Qt 6.8) — `traduku`; `ToolTip` пишет сам продукт («Traduku — Press Ctrl+C twice…»), и на обоих гостях он одинаков | `test_04` на arch-kde и ubuntu-gnome |
| **display name задаёт `configureApplication`** | KDE-`main.cpp` ставил его, GNOME-ветка — нет: две оболочки настраивали один продукт по-разному; machine-id (config, D-Bus, пакет) остаются строчными | `tst_cli::panelNameIsTheSameOnBothShells` |
| **GNOME-пакет сам кладёт autostart в `/etc/xdg/autostart`** | диалога первого запуска в GNOME-оболочке ещё нет (Stage 6), а «поставил → ребут → жму жест → тишина» — не результат; путь абсолютный: `CMAKE_INSTALL_SYSCONFDIR` при префиксе `/usr` даёт `/usr/etc/xdg/autostart`, что ни одна сессия не читает | `test_02a` на ubuntu-gnome, `test_03` после ребута |
| **appindicator включается дефолтом dconf при сборке образа** | GNOME Shell сканирует каталог расширений до того, как кто-то может отреагировать: включить после старта шелла = не включить; Ubuntu отдаёт расширение выключенным, а `enabled-extensions` не имеет fallback — угадал неверно, остаёшься без трей-хоста | `mkvm.sh` (`30-tray`); без хоста `test_04` skip с этой причиной |
| **харнес не даёт KDE-команды GNOME-гостю** | `restart_kwin()` в test_05 писал kxkbrc и убивал gnome-shell → семь красных тестов из-за одной команды; `set_layouts()` выбирает механизм десктопа (gsettings `input-sources` / kxkb+KWin), общий гейт `_gesture_is_alive` доказывает жест на любом десктопе | e2e 05 на arch-kde и ubuntu-gnome |
| **прогон переиспользует: qcow2-оверлей и кэш `.deb` по хэшу содержимого** | golden-диск — post-install состояние, значит испорченный гость стоит удаления файла, а не пересборки образа; кэш по mtime врёт после `git checkout` (mtime сохраняется) и обслуживает протухший пакет | `vmctl.sh start <vm> scratch` / `reset`; замеры ниже в 4.5 |

Отброшенные гипотезы, чтобы не возвращались: собственный `SniTray` +
`com.canonical.dbusmenu` (референс остался в `tools/spike/tray-spike.cpp`),
`GestureService` на D-Bus и расширение GNOME Shell, focus-трюк для буфера,
портальные хоткеи, группа `input`, `99-*` имя правила, Qt-`QClipboard` на
Wayland, сборка в гостях.

---

## 3. Состояние

### Тесты

| | |
|---|---|
| юнит/интеграция, `LT_SHELL=kde` | **33/33** |
| юнит/интеграция, `LT_SHELL=gnome` | **33/33** |
| e2e `arch-kde` (пакеты, pacman) | **8/8** |
| e2e `ubuntu-gnome` (пакеты, apt) | **8/8** |

### Грид (`tools/vms/`, профили `arch-kde debian-kde debian-gnome ubuntu-kde ubuntu-gnome`)

| профиль | пакет | примечание |
|---|---|---|
| `arch-kde` | `traduku-0.3.0-1` | зелёный целиком |
| `ubuntu-gnome` | `traduku-gnome_0.3.0-1` | зелёный целиком: пакетный autostart переживает ребут, appindicator отдаёт трей-хост, жест/`wl-paste`/окно работают на живом Wayland; образ пересоздан с фиксами `mkvm.sh` (display manager + dconf) |
| `debian-kde`, `debian-gnome`, `ubuntu-kde` | путь `.deb` проложен | не поднимались в этой сессии |

### Этапы GNOME

0 спайки · 1 каркас+CLI · 1b отключение reasoning · 2 уведомления+один экземпляр ·
3A жест evdev · 5 `QtTray` · 6b паритет трея и удаление `KdeTray` ·
7 частично (`ubuntu-gnome`: e2e 8/8, образ пересоздан) · 8 частично
(версия 0.3.0, два флейвора, udev в пакете) — **готово**.
4 (`X11Clipboard`), 3B (gsettings), 6 (первый запуск/HiDPI/звук),
остаток 7 (`debian-gnome`, `ubuntu-kde`, `debian-kde` не поднимались)
и остаток 8 (README, релиз) — ниже.

---

## 4. Что делать (порядок = список)

### 4.1 Stage 4 — `X11Clipboard` и цепочка приоритетов

`XGetSelectionOwner`/`XConvertSelection` на чтение,
`XSetSelectionOwner` + ответ на `SelectionRequest` на запись,
`XAUTHORITY` из `/proc/<Xwayland-pid>/environ`, общий с `WlClipboard`
разбор MIME/charset (`UTF8_STRING`, `TEXT`, `TIMESTAMP`…). Порядок в
`ClipboardFactory`: `wl-paste` → X11 → Qt. Это закрывает обещание
«Ubuntu 24.04 / Debian 13» и не зависит от VM.

### 4.2 Stage 6 — автономность GNOME-оболочки

Первый запуск (что нажимать, если иконки нет), иконки Yaru/Adwaita, HiDPI,
нелатинская локаль, звук через PipeWire, перелогин → процесс жив.
KDE-диалог первого запуска при этом не должен переехать в GNOME: там
автозапуск из пакета.

### 4.3 Stage 3B — gsettings-fallback (по критерию, не «на всякий случай»)

`org.gnome.settings-daemon.plugins.media-keys custom-keybindings` +
`gdbus call … dev.traduku.Traduku gesturePressed`. Делать, когда появится
реальная система, где evdev недоступен (нет uaccess, `/dev/input` закрыт
политикой). До этого — не писать: evdev работает, а второй путь обязан ещё
и тестироваться.

### 4.4 Stage 8 — релиз

- README: таблица «что работает на каком DE/сессии», «какой пакет куда»,
  раздел про udev-правило и почему группа `input` — плохое решение,
  раздел про `Suggests:` расширение (без иконки всё работает).
- Релиз с пакетами для каждой платформы + проверка установки из них в
  гостях (это уже делает `test_01`).
- Теги. В `master` не пушить без явного запроса.

### 4.5 Скорость VM-цикла — измерено

Один прогон `ubuntu-gnome`, 13:00–13:20: сборка `.deb` в контейнере **71 s**
(было ~6,5 min: гейт больше не конфигурирует лишние два дерева), кэш по
хэшу содержимого на незаменённом дереве — **3 s**, установка в гостя **2 s**,
`vm-install.sh` целиком **74 s**, полный e2e-прогон **2:36** (8 тестов),
два теста на scratch-госте без установки пакета — **39 s**.

Закрыто: `start <vm> scratch` (оверлей поверх golden-диска) + `reset`
(удаление оверлея, отказ при запущенном госте), кэш `.deb` в `.cache/vm-deb`
с fingerprint от `git ls-files -co --exclude-standard`, verbose-первая
попытка сборки, тайминги в последней строке — только те числа, которые
ветка действительно измеряла.

Осталось: пересоздать `ubuntu-kde`, `debian-kde`, `debian-gnome` (образ
`ubuntu-gnome` пересоздан), и посмотреть ccache в контейнере-сборщике, если
сборка снова станет самым долгим шагом. Разработческий прогон может ставить
**хостовый** бинарник, но CI обязан ставить пакет — иначе Depends никто не
проверяет.

Отдельно, заодно с замерами: `mkrelease.sh` копировал только `PKGBUILD`, и
с тех пор как в нём появился `install=`, makepkg падал на
`traduku-gnome.install does not exist`, а `vm-install` этого не замечал —
pacman не ставил ничего, и все гейты зелёнили **уже стоявший** в госте
пакет. Теперь вспомогательные файлы копируются, а ветка Arch обязана
выдать пакет новее начала прогона. Красная сборка, которую харнес спутал с
зелёным прогоном, дороже любой скорости.

### 4.6 Гигиена

- `tests/cmake/test_versions_agree.sh` уже валит сборку при дрейфе версий;
  добавить в него `README.md`, когда там появится строка с версией.
- `lintian` на GNOME-пакет — после того, как у него появятся пользователи.
- пакеты собираются из `git archive HEAD`: **сначала коммит, потом сборка**.

---

## 5. Вопросы оператору (не моя работа, а решение)

1. evdev = ACL на чтение клавиатуры активной сессии, которое ставит
   пакет. Оставляем как путь по умолчанию с явным текстом в README, или
   делаем опцией конфига `false` по умолчанию?
2. Система без XWayland и без `wl-clipboard` буфер не читает вообще.
   Нужен ли там явный отказ с объяснением (и тогда какой — диалог,
   уведомление, только лог)?
3. Расширение-хост трея: оставили `Suggests`. Достаточно, или на GNOME его
   стоит предлагать в первом запуске одной строкой?

---

## 6. Инфраструктура: что где лежит

Надо помнить одну команду — `make help`; он печатает остальные и объясняет,
сколько каждая стоит. `Makefile` — только слой алиасов: в нём нет ни одного
файлового правила, все цели `.PHONY` и зовут тот инструмент, который знает
про зависимости (ninja/cmake — про код, хэш-кэш в `vm-install.sh` — про
пакеты). Зависимости, заведённые здесь, стали бы вторым мнением о том, что
протухло, а два мнения уже приводили к зелёному прогону на старом пакете.

```
make build | test | selftest | install      # хост: ninja/ctest/install в ~/.local
make up|down|scratch|reset|check|guest|e2e|cycle|ssh|shot  VM=<vm>
                                 # грид; cycle = пакет -> гость -> e2e одной командой
```
tools/vms/vmctl.sh up|mkvm|start [scratch]|reset|ssh|keyseq|shot|vnc|status
                                # грид; scratch = прогон на оверлее, reset = сброс
tools/vms/check-grid.sh <vm>        # «это машина пользователя»: сессия, DE, пакетный менеджер
tools/vms/vm-install.sh <vm>        # собрать пакет (контейнер) + поставить + гейт ctest
packaging/arch/mkrelease.sh release-out   # traduku + traduku-gnome (.sig)
tools/mock-openai.py [8931]         # mock OpenAI-эндпоинт для e2e
tests/e2e/  TRADUKU_VM=<vm> .venv/bin/python -m pytest tests/e2e -q
```

Уровни не смешивать: юнит/интеграция/сборка/упаковка — контейнеры;
GUI — QEMU+QMP+SSH (`input-send-event` = scancode уровня железа,
`screendump` = буфер консоли независимо от композитора).
`openQA` — кандидат на «тест глазами» (инсталлятор, темы, первый запуск),
когда матрица разрастётся; `dogtail/qecore` — ввод на Wayland только через
GNOME-порталы, на Plasma инжектить нечем; `distrobox` — уровни 1–2, GUI-уровня
у него нет.

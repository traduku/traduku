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
| **Пакунки сами применяют правило** | файл правила не меняет ACL уже существующих устройств → «поставь и перезагрузись» для программы, у которой единственный интерфейс — клавиши | postinst / `.install`-хук, проверено в госте |
| **Два флейвора из одного дерева**: `traduku` (KF6) и `traduku-gnome` (без KF6) | `LT_SHELL=auto` брал KDE, стоило KF6 оказаться в системе; GNOME-слой обязан быть KF6-свободным — это проверяется `ldd` | `test_01` на arch-kde и ubuntu-gnome |
| **Один CLI на обе оболочки** (`app/cli.cpp`) | KDE-`main.cpp` годами держал свой парсер: `--version` без номера, `--backend-info` = «Unknown option», своя копия one-shot с другим текстом ошибки | `tst_cli`, `--backend-info` на Plasma |
| **Гости ничего не компилируют** | доставка — пакунки; компиляция в госте проверяла хост и маскировала отсутствующие runtime-Depends | `vm-install.sh`, `check-grid.sh` |

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
| e2e `arch-kde` (пакунки, pacman) | **8/8** |
| e2e `ubuntu-gnome` (пакунки, apt) | **4/8** |

### Грид (`tools/vms/`, профили `arch-kde debian-kde debian-gnome ubuntu-kde ubuntu-gnome`)

| профиль | пакунок | примечание |
|---|---|---|
| `arch-kde` | `traduku-0.3.0-1` | зелёный целиком |
| `ubuntu-gnome` | `traduku-gnome_0.3.0-1` | 05/07/08 зелёные = evdev-жест, `wl-paste` и запись результата на живом Wayland |
| `debian-kde`, `debian-gnome`, `ubuntu-kde` | путь `.deb` проложен | не поднимались в этой сессии |

### Этапы GNOME

0 спайки · 1 каркас+CLI · 1b отключение reasoning · 2 уведомления+один экземпляр ·
3A жест evdev · 5 `QtTray` · 6b паритет трея и удаление `KdeTray` · 8 частично
(версия 0.3.0, два флейвора, udev в пакете) — **готово**.
4 (`X11Clipboard`), 3B (gsettings), 6 (первый запуск/HiDPI/звук),
7 (грид-GNOME) и остаток 8 (README, релиз) — ниже.

---

## 4. Что делать (порядок = список)

### 4.1 e2e на GNOME — довести до 8/8

Три красных теста красны не из-за продукта, а из-за того, что харнес знает
только KWin. Каждый чинится отдельно и с причиной в имени.

1. **`test_06_window`** — список окон это запрос к KWin-скриптингу
   (`'Traduku' in ''` на GNOME). Нужен desktop-aware способ: на X11 —
   `xdotool search --name`, на Wayland — journal-строка приложения об
   открытии окна (у нас она есть) + скриншот-диф по QMP.
2. **`test_04_tray`** — в госте нет `org.kde.StatusNotifierWatcher`.
   `mkvm.sh` теперь ставит `gnome-shell-extension-appindicator`, но его надо
   **включить** пользователю (`gnome-extensions enable
   ubuntu-appindicators@ubuntu.com` / `org.gnome.shell enabled-extensions`).
   Тест должен искать предмет на том watcher'е, который у DESKTOPA есть, и
   явно `SKIP`иться без него — vacuous-зелёный хуже красного.
3. **`test_02`/`test_03`** — они жмут Enter в KDE-диалоге первого запуска,
   которого в GNOME нет (там XDG-autostart из пакунка). Либо `skip` с
   причиной, либо отдельный сценарий «пакунковый autostart живёт после
   ребута».

Потом — **пересоздать образы**, иначе фиксы `mkvm.sh` живут только в
запущенной VM:

```bash
./tools/vms/vmctl.sh mkvm ubuntu-gnome ubuntu gnome
./tools/vms/vmctl.sh mkvm ubuntu-kde   ubuntu kde
./tools/vms/vmctl.sh mkvm debian-gnome debian gnome
./tools/vms/vmctl.sh mkvm debian-kde   debian kde
./tools/vms/check-grid.sh <vm>          # ворота перед прогоном
```

Прогоны гонять **последовательно**: два грида одновременно на 6 ядрах дают
тайминг-флейки (измерено: 1 фейл из 8 прогонов).

### 4.2 Stage 4 — `X11Clipboard` и цепочка приоритетов

`XGetSelectionOwner`/`XConvertSelection` на чтение,
`XSetSelectionOwner` + ответ на `SelectionRequest` на запись,
`XAUTHORITY` из `/proc/<Xwayland-pid>/environ`, общий с `WlClipboard`
разбор MIME/charset (`UTF8_STRING`, `TEXT`, `TIMESTAMP`…). Порядок в
`ClipboardFactory`: `wl-paste` → X11 → Qt. Это закрывает обещание
«Ubuntu 24.04 / Debian 13» и не зависит от VM.

### 4.3 Stage 6 — автономность GNOME-оболочки

Первый запуск (что нажимать, если иконки нет), иконки Yaru/Adwaita, HiDPI,
нелатинская локаль, звук через PipeWire, перелогин → процесс жив.
KDE-диалог первого запуска при этом не должен переехать в GNOME: там
автозапуск из пакунка.

### 4.4 Stage 3B — gsettings-fallback (по критерию, не «на всякий случай»)

`org.gnome.settings-daemon.plugins.media-keys custom-keybindings` +
`gdbus call … dev.traduku.Traduku gesturePressed`. Делать, когда появится
реальная система, где evdev недоступен (нет uaccess, `/dev/input` закрыт
политикой). До этого — не писать: evdev работает, а второй путь обязан ещё
и тестироваться.

### 4.5 Stage 8 — релиз

- README: таблица «что работает на каком DE/сессии», «какой пакунок куда»,
  раздел про udev-правило и почему группа `input` — плохое решение,
  раздел про `Suggests:` расширение (без иконки всё работает).
- Релиз с пакунками для каждой платформы + проверка установки из них в
  гостях (это уже делает `test_01`).
- Теги. В `master` не пушить без явного запроса.

### 4.6 Скорость VM-цикла — сначала измерить

Сейчас таймингов одного полного прогона (`mkvm → up → install → pytest`) нет,
и оптимизировать на глаз рано. Кандидаты: btrfs-снапшоты базовых образов,
переиспользование qcow2-оверлеев, ccache/предсобранные зависимости в базе,
прогрев гостей. Разработческий прогон может ставить **хостовый** бинарник,
но CI обязан ставить пакунок — иначе Depends никто не проверяет.

### 4.7 Гигиена

- `tests/cmake/test_versions_agree.sh` уже валит сборку при дрейфе версий;
  добавить в него `README.md`, когда там появится строка с версией.
- `lintian` на GNOME-пакунок — после того, как у него появятся пользователи.
- Пакунки собираются из `git archive HEAD`: **сначала коммит, потом сборка**.

---

## 5. Вопросы оператору (не моя работа, а решение)

1. evdev = ACL на чтение клавиатуры активной сессии, которое ставит
   пакунок. Оставляем как путь по умолчанию с явным текстом в README, или
   делаем опцией конфига `false` по умолчанию?
2. Система без XWayland и без `wl-clipboard` буфер не читает вообще.
   Нужен ли там явный отказ с объяснением (и тогда какой — диалог,
   уведомление, только лог)?
3. Расширение-хост трея: оставили `Suggests`. Достаточно, или на GNOME его
   стоит предлагать в первом запуске одной строкой?

---

## 6. Инфраструктура: что где лежит

```
tools/vms/vmctl.sh up|mkvm|start|ssh|keyseq|shot|vnc|status   # грид
tools/vms/check-grid.sh <vm>        # «это машина пользователя»: сессия, DE, пакетный менеджер
tools/vms/vm-install.sh <vm>        # собрать пакунок (контейнер) + поставить + гейт ctest
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

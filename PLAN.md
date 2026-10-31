# llm-translate — план переписывания

Переписываем `~/Projects/kde_translate` (C++/Qt6/KF6, прототип) с нуля как
модульное приложение. Прототип остаётся эталоном поведения; новый код
проектируется так, чтобы ядро и UI можно было собрать под GNOME (и любую
DE/ОС с Qt), меняя только тонкий слой платформы.

## Ключевые проектные решения

1. **Модульность — 4 слоя, зависимости направлены вниз:**
   - `core` — чистый Qt (Core/Network/Multimedia), НИКАКИХ KF6/X11/DBus:
     настройки, языки, SSE-парсер стриминга, LLM-клиент, движок перевода,
     детектор двойного Ctrl+C, проигрыватель звуков. Всё покрыто unit-тестами.
   - `interfaces` — абстракции платформенных сервисов: `IClipboard`,
     `IHotkeyBackend`, `INotifier`, `IAutostart`, `IGlobalShortcut`.
   - `platform/<имя>` — реализации: `qt` (QClipboard), `wl` (wl-clipboard),
     `x11` (пассивный монитор клавиатуры XI2/RECORD), `kde` (KStatusNotifierItem-трей,
     KNotification, KGlobalAccel, autostart). Позже `gnome`
     (AppIndicator + libei/GSettings) подключается без правки core/ui.
   - `ui` — QWidget-окна (окно перевода, настройки), только Qt Widgets.
   - `app/kde` — composition root: собирает core+ui+platform(kde,x11,qt,wl).
2. **Окно, не виджет.** DeepL-параллакс «оригинал | перевод» с прокруткой,
   кнопками и ресайзом невозможен в тултипе трея/виджете плазмы; виджет
   Plasma — только KDE, а нам нужен и GNOME. Решение: обычное Qt-окно
   (`TranslatorWindow`), открываемое поверх, с автопоказом при старте
   перевода; трей лишь вызывает его. Окно lives в `ui/`, DE-независимо.
3. **Стриминг.** LLM-API вызывается с `"stream": true`; собственный
   инкрементальный SSE-парсер выдаёт дельти в `TranslationEngine`, тот —
   в окно и в буфер; по `finish_reason`/`[DONE]` — финализация, звук
   готовности, запись в буфер обмена. Пользователь видит текст «вживую».
4. **Звуки сохраняем:** начало перевода (`bell.oga`), готово
   (`complete.oga`), ошибка (`dialog-error.oga`) из freedesktop-темы;
   `SoundPlayer` в core (QSoundEffect с fallback на paplay/aplay, тема
   инициализируется инъекцией — тестируется без звука).
5. **TDD:** для каждого модуля сначала пишутся тесты (unit для core/ui,
   интеграционные для клиента+движка со встроенным mock-сервером на
   QTcpServer — hermetic, без python), затем реализация до зелёного.
   Каждый завершённый модуль — отдельный git-коммит.

## Архитектура

```
src/core/        languages, settings, doublepressdetector, sseparser,
                 llmstreamclient, translationengine, soundplayer
src/interfaces/  iclipboard, ihotkeybackend, inotifier, iautostart,
                 iglobalshortcut          (чистые C++/Qt абстракции)
src/platform/qt/ QtClipboard
src/platform/wl/ WlClipboard (wl-paste/wl-copy)
src/platform/x11/X11HotkeyBackend (XI2 raw + RECORD fallback)
src/platform/kde/KdeTray, KdeNotifier, KdeGlobalShortcut, KdeAutostart
src/ui/          TranslatorWindow, SettingsDialog
src/app/kde/     main.cpp (kde-сборка, CLI: --translate/--selftest)
tests/unit/      QtTest, по файлу на модуль core/ui/platform(qt,wl)
tests/integration/ движок+клиент+mock-сервер; полный пайплайн
tools/mock-openai.py  стриминговый mock для ручной проверки
```

## Модули (порядок коммитов)

| # | Модуль | Тесты (пишутся ПЕРЕД реализацией) |
|---|--------|--------------------------------|
| M0 | Каркас: git, CMake, CTest/QtTest harness | smoke-тест harness |
| M1 | core: languages, settings (QSettings INI, без KConfig) | дефолты, round-trip save/reload, дефолтный язык ru |
| M2 | core: DoublePressDetector (чистая машина, инъекция часов) | одинарный Ctrl+C, двойной в окне, вне окна, автоповтор, без Ctrl, «c» в раскладках (keycode) |
| M3 | core: SseParser (инкрементальный OpenAI-SSE) | partial lines, split UTF-8, [DONE], ошибки, surrogates |
| M4 | core: LlmStreamClient (POST stream=true) | интеграционный: mock QTcpServer шлёт SSE кусками; auth header; таймаут; 4xx/5xx |
| M5 | core: TranslationEngine (промпт + джобы) | unit промпта; интеграция engine+client: started/delta/finished/failed, busy-lock |
| M6 | interfaces + clipboard: QtClipboard, WlClipboard, выбор бэкенда | factory: platform->backend, fallback, read/write (фейковый tool-runner) |
| M7 | core: SoundPlayer (события start/finish/fail) | разрешение путей темы, fallback-цепочка, no-sound-режим, инъекция проигрывателя |
| M8 | ui: TranslatorWindow + SettingsDialog | offscreen: стрим дельт в поле, кнопки copy/close, load/save настроек |
| M9 | platform/x11: X11HotkeyBackend + GlobalShortcutMonitor (double-press переиспользует M2) | unit: детектор+монитор на фейковых событиях; X-склейка тонкая, описана |
| M10 | platform/kde + app/kde: трей, нотификатор, autostart, main, CLI | интеграционный полный пайплайн: двойное нажатие → стрим → окно → буфер → звуки (на фейковых бэкендах) |
| M11 | Полировка: README, install.sh, desktop-файлы, notifyrc, selftest CLI | финальный `--selftest` + ctest |

## Статус

- [ ] M0 … M11 — обновляется по мере работы.

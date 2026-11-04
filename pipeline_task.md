Необходимо подготовить среду для тестирования GUI приложения в разных ОС и DE.

Правильная схема — не «гонять всё приложение по матрице из 8 дистрибутивов × 4 DE», а разложить тесты на три уровня: контейнеры для сборки и упаковки, реальная графическая сессия для GUI, и сокращённая матрица «крайних точек» вместо полного перебора.

## Три уровня, которые не надо смешивать

| Уровень | Что проверяет | Где запускать |
|---|---|---|
| Unit / интеграционные | логика, парсеры, D-Bus-интерфейсы | контейнеры (podman/docker) на каждый дистрибутив |
| Сборка и упаковка | что `.deb`/`.rpm`/pkg.tar.zst собирается и ставится | тот же контейнер, что и уровень 1 |
| GUI / E2E | что окно открывается, кнопки нажимаются, portals и темы работают | VM или вложенная Wayland-сессия, никогда не голый Xvfb |

Не пытаться запускать GUI-тесты в обычном контейнере с Xvfb. Xvfb даёт X11 без композитора, без `xdg-desktop-portal`, без настройки DE, то есть ровно те вещи, которые чаще всего и ломаются.

## Контейнеры: сборка, зависимости, пакеты

Матрица для уровней 1–2 — это дешёвая часть, её можно делать широкой. Тестируемый набор:

- **Debian stable** — самый старый glibc/GTK/Qt, ловит «слишком новые зависимости».
- **Ubuntu LTS** — то, что реально стоит у большинства пользователей.
- **Fedora** — свежие библиотеки и другой набор патчей.
- **Arch** — ловит баги, которые вылезут через полгода, плюс отсутствие устаревших API.

В каждом контейнере делайте полный цикл: поставить build-deps, собрать, собрать пакет, установить пакет, запустить бинарь с `--version` и коротким smoke-тестом. Это отлавливает 80% проблем упаковки без всякой графики.
Локально ту же задачу решает distrobox: он создаёт контейнеры с любым дистрибутивом, «плотно интегрированные с хостом, включая графические приложения X11/Wayland и звук». [packages.debian](https://packages.debian.org/testing/distrobox)

Важная деталь гигиены CI: пиньте образы по digest, а не по тегу `latest` — на это прямо указывают в материалах openQA: «take care with upstream containers — pin versions using container hash». [archive.fosdem](https://archive.fosdem.org/2023/schedule/event/openqa_for_gnome/attachments/slides/5742/export/events/attachments/openqa_for_gnome/slides/5742/OpenQA.pdf)

## GUI: настоящая сессия внутри VM или вложенного композитора

Если вы пишете на GTK/Qt, самый дешёвый путь — тестировать через accessibility-дерево, а не через координаты пикселей. `dogtail` работает через AT-SPI и поддерживает как Xorg, так и Wayland GNOME. Для Wayland нужен `gnome-ponytail-daemon`, который инжектит ввод через Screen Cast и Remote Desktop API; а `dogtail-headless` умеет логиниться в реальную сессию через GDM даже в контейнере без графики. Поверх этого обычно ставят `qecore`: его скрипт `qecore-headless` конфигурирует сессию, запускает каждый тест в новой GDM-сессии, умеет проверять тип сессии (X11 vs Wayland) и не даёт тестам «заражать» друг друга. [pypi](https://pypi.org/project/dogtail/)

Связка `behave` + `qecore-headless` + `dogtail` — это фактически стандарт в Fedora/GNOME-мире: Gherkin-сценарии, шаги на Python, HTML-отчёт с видео и скриншотами при падении. Хороший живой пример — тестовый пайплайн Project Bluefin: GNOME-наборы идут headless Wayland через `qecore-headless` + dogtail, KDE Plasma управляется через `selenium-webdriver-at-spi`, и всё это крутится в QEMU на обычных GitHub Actions раннерах без своего железа. [fedoramagazine](https://fedoramagazine.org/automation-through-accessibility/)

Если нужен тест «глазами» — установка, первый запуск, отрисовка темы, диалоги — берите openQA. Он грузит образ в VM, шлёт нажатия и клики и сверяет экран по эталонным скриншотам («needles») с нечётким сравнением через OpenCV. GNOME гоняет так GNOME OS в GitLab CI, поднимая временный worker под каждый job; полный прогон занимает меньше пяти минут. [codethink.co](https://www.codethink.co.uk/articles/2021/gnome-with-openqa/)

## Что реально ломается в разных DE

Планируйте тесты вокруг конкретных точек расхождения, а не «на всякий случай»:

- **Трей и StatusNotifierItem** — в GNOME без расширения иконки просто не появятся.
- **Portals** — `xdg-desktop-portal-gtk` vs `-kde` vs `-gnome`: файловые диалоги и скриншоты ведут себя по-разному.
- **Темы и иконки** — отсутствие adwaita-icon-theme или Breeze даёт пустые иконки и битую вёрстку.
- **HiDPI и масштабирование** — дробное масштабирование в GNOME и KDE считается по-разному.
- **Шрифты и локаль** — проверяйте запуск с не-английской локалью, иначе поймаете обрезанный текст уже у пользователя.
- **X11 vs Wayland** — глобальные хоткеи, `XGrabKey`, позиционирование окон: на Wayland многое запрещено.

## Как сократить матрицу

Не тестируйте декартово произведение. Держите отдельно: 4 дистрибутива × 1 DE для сборки/упаковки (контейнеры, дёшево) и 2 DE (GNOME + KDE) × 1–2 дистрибутива для GUI (VM, дорого). X11 достаточно прогнать на одном дистрибутиве как отдельный job, потому что это свойство сессии, а не дистрибутива.


# Tests

Следуй философии TDD. Не изменять уже существующие тесты без согласования. Если ты обнаружил, что тест сломан или тестирует не то - обсуди со мной.

Тесты для Pipeline (еще не существуют), дополнять при необходимости:
- [x] тест установки программы стандартным для дистрибутива пакетным менеджером из terminal
      (test_01: подписанный pkg.tar.zst → `sudo pacman -U`, key trust, desktop entry, selftest; GREEN arch-kde, red-check ✓)
- [x] тест установки в автозапуск.
      (test_02: DE не запрещает XDG-запись, но программа спрашивает при первом запуске —
       приветственный диалог + Enter(Yes) → ~/.config/autostart/traduku.desktop; GREEN, red-check ✓)
  - Если ОС/DE запрещает авто-установку в автозапуск - тест запроса пользователю на автозапуск
  - если запрос пользователю невозможен в терминале - тест запроса при первом запуске программы
- [x] тест автозапуска программы при рестарте ОС
      (test_03: systemctl reboot → сессия → процесс живёт, XDG_SESSION_TYPE=wayland; GREEN, red-check ✓)
- [x] тест наличия иконки в трее DE
      (test_04: org.kde.StatusNotifierWatcher RegisteredItems + Title=Traduku через gdbus
       + скриншот панели + in-test red-check «убил приложение — иконка пропала»; GREEN)
- [x] тест вызова по сочетанию клавиш CTRL+C+C (также в разных раскладках)
      (test_05: GREEN arch-kde + red-check ✓ (PressInterval 0.18 → жест молчит → откат → green);
       KGlobalAccel-пара ctrl+alt+c×2 через QMP scancodes, одиночное нажатие пассивно,
       раскладка ru идентична (kxkbrc + Alt+Shift). Нюансы: приложение перезаписывает
       config.ini на exit; KGA/KWin bounce после restart_kwin)
- [x] тест появления окна программы (test_06: GREEN arch-kde + red-checks A,B ✓.
       Причина прошлого фейла: grep журнала --since -20s ловил 'translating' от test_05 →
       скриншот в момент анимации (менялся только таскбар). Fix: journal-cursor ДО жеста
       (--after-cursor, взаимоисключающ с --since), idle-guard (<0.01 frac) +
       отключённый idle-lock (kwinidlerc, powerdevil; шум = экран блокировки, не обои),
       ассерт через KWin scripting D-Bus: [Traduku 980x588 act=true].
       Auto-hide в приложении НЕТ (нет hide в src/) — assertion убран, задача = появление.)
- [x] тест перехвата программой буфера обмена стандартного для конкретных DE
      (test_07: GREEN arch-kde + red-check ✓. wl-copy seed → KGA-жест → результат содержит seed+MOCK.
       Жест = KGA-пара (ctrl+alt+c×2): XI2-слушатель на KDE/Wayland структурно мёртв
       (KWin инжектит клавиши в XWayland в окно фокуса, XI2 RAW-событий нет —
       проверено xinput --root = 0 RAWKEY). XI2-покрытие — за X11-джобом (debian).
       Найден и исправлен vacuous-assert: 'SEED in got' проходил и без жеста.)
- [x] тест вставки результата перевода обратно в буфер обмена
      (test_08: GREEN arch-kde + red-check ✓. CopyResult=true → wl-paste начинается с «[MOCK -> ".
       Уточнение: mock эхо-ит ИМЯ языка («[MOCK -> English]»), не код — ожидаение поправлено.)

# Plan
- [x] ознакомиться с проектом (README.md, ARCHITECTURE.md)
- [x] сравнить  behave + qecore-headless + dogtail   против  openQA  против distrobox  на основе их описаний для нашего набора тестов  и для нашей матрицы Debian/ubuntu/fedora/arch   kde/gnome, фокус на Wayland (но не отказываться от XWayland специально если он уже поддерживается программой).
      Итог: dogtail/qecore — ввод на Wayland только через gnome-ponytail (GNOME-специфично), на KDE Plasma Wayland нечем инжектить; openQA — полный охват, но тяжёлая инфраструктура и пиксельные needles; distrobox — идеален для уровней 1–2, GUI-тесты невозможны.
- [x] после выбора подходящего пайплайна создать PIPELINE.md с краткой документацией, коммит
      (PIPELINE.md: контейнеры для build/package, QEMU-грид (SSH+QMP) для GUI/E2E, commit 62140ff)
    
Подготовить виртуальное тестовое окружения согласно PIPELINE.md для:
- [x] arch/kde (родное окружение для программы)
      Arch-базa собрана из ОФИЦИАЛЬНОГО bootstrap-тарбола (cloud-образов у Arch нет):
      base+plasma-desktop+sddm (автологин Plasma Wayland) + build-deps; загрузка QEMU -kernel
      (загрузчик не нужен); пользователь vm/vm; приложение УСТАНАВЛИВАЕТСЯ в VM бинарным
      подписанным пакетом (host makepkg --sign → scp → pacman -U), VM ничего не компилирует.
      Найдено по ходу: приложениям на KDE/Wayland нужен qt6-wayland (платформенный плагин) —
      добавлен в базовый образ и в зависимости сборки; systemd-resolved+DNS для сети QEMU user-net.
- [x] создать тесты один за одним тестируя каждый тест против программы (тесты 1–8 готовы и зелёные, каждый с red-check)
  - [x] обязательно тестировать тесты на падение (для 1–4 доказано: саботаж → FAIL → откат → PASS)
  - [x] делать коммит на каждый готовый тест
- [x] прогнать тесты под arch/kde (остановиться и доложить о результате (также при наличии ошибок)
      ДОКЛАД 2026-11-01: 8/8 зелёных ×3 прогона подряд (стабильно, ~2:20 за прогон), коммит 76d15e9.
      починено в процессе (см. коммит): mock-сервер умирал на reboot; конфиг-по-умолчанию
      зтирал mock-конфиг (приложение пересохраняет config.ini на exit — конфиг пишется
      при остановленном приложении, в app_running); два GPU в QEMU → KWin dual-head →
      окно уезжало за границу скриншота (-vga none); KGA-гонка после restart kwin
      (e2e warmup-гейт по journal-cursor).

- [x] подготовить debian/kde окружение
      Готово (2026-11-02): p-debian-kde = Debian 13 trixie cloud image + kde-plasma-desktop
      (Plasma 6.3.6, Wayland-сессия, sddm-autologin; ssh :2206 vnc :5906). Починено по ходу:
      vmlife.sh -device virtio-gpu-pci -> virtio-vga (без VGA-совместимости SeaBIOS не
      выводит GRUB/kernel: «Display output is not active», гостевой цикл ребута).
      Особенности Plasma 6.3 (отличия от arch 6.8, проверены живьем):
      (1) сессия принадлежит sddm/startplasma-wayland, НЕ user-юниту: systemctl --user
      restart plasma-kwin_wayland убивает композитор навсегда (юнит-враппер выбирает
      backend из окружения юзер-менеджера, DRM-мастер умиращий, plasmashell/kded в таймауте,
      сиротский /tmp/.X11-unix/X0, все wl/X-клиенты висят) -> фикстура бунтит kwin только на
      arch (kglobalaccel bounce общий);
      (2) clipboard/klipper/kwin-scripting/org.kde.KWin — живые при здоровой сессии (проверено
      roundtrip'ом wl-copy/wl-paste и print() из Scripting D-Bus).
      Ресурсы: не гонять два грида-сюита одновременно (debian test_03 reboot storm +
      4 vCPU x2 на 6-core хосте = тайминг-флейки test_02/03 на arch; последовательные прогоны
      стабильны: arch 8/8 x2, debian 8 failed за 2:25).
- [x] большинство или все тестов ожидаемо должно падать в debian/kde (пакеты под платформу еще не готовы, другое окружение и так далее)
      Было так на неустановленном приложении (8 failed/0 errors, продуктовые причины).
      ЗАКРЫТО РАНЬШЕ СРОКА (2026-11-03): .deb-слой готов (debian:13 контейнер,
      dpkg-buildpackage, коммит cab4e0c) — и debian/kde даёт 8/8 ЗЕЛЁНЫЕ (×7 прогонов,
      ~2:30). Что починилось по пути:
      (1) control: «Description-» (парсер вешался), имена рантайм-зависимостей
          (qt6-multimedia → libqt6multimedia6; + qt6-wayland — тот же урок, что arch
          находит №2, здесь как настоящий Depends);
      (2) контейнер: cp ташил хостовый build/ (Qt 6.12) → rm -rf build; set -e (gate
          не имел права голоса — его rc съедал cp); nocheck+отдельный gatebuild
          (debhelper на nocheck ставит BUILD_TESTING=OFF — квест vacuous);
      (3) journal-канал: запуск через setsid nohup >файл делал journal_after слепым
          (arch спасался autostart-инстансом, писавшим в journal; debian честил
          05/06 за «нет translating»). Теперь Grid.start_app() = systemd-run
          transient-юнит с полным session env; один канал на обеих дистрибутивах;
      (4) test_06 ассерт размера: KWin 6.8 «980x588» vs 6.3 «982x589» (учёт рамок) —
          строковый префикс заменён числовым коридором 970..992 × 550..610.
      Флейк: debian 1 из 8 прогонов (сразу после arch-сюита; не воспроизвёл)
      — подозревается хост-CPU-контеншен двух гридов; прогоны гонять последовательно.
- [x] оценить правильность выбора стратегии тестирования GUI, не нужно ли сменить платформу (openQA or distrobox or ...)?
      Итог: платформа верная. QEMU+QMP+SSH = грид с детерминированным вводом (scancode-клавиши,
      screendump независим от композитора). openQA — надстройка над тем же QEMU (те же scancode
      и needles-пиксели), целесообразна позже для installer/first-run/тем, не заменяя грид;
      distrobox — уровней 1–2, GUI-уровня у него нет. Смена платформы ничего не даёт на Wayland.

## Расследование EIS-попапа (2026-11-01, коммит df312ea) — закрыто
Задача: «протестировать клик по попапу KWin 6.8 (разрешение на чтение клавиатуры)».
Измержено end-to-end на arch-kde (включая установку cachyos-патченного xorg-xwayland -1.1,
трассировку EIS-сокета, скриншоты на 2.5s/14s, xinput --root c stdbuf, QMP-мышь):
- Текущий traduku (пассивный XI2, без XGrabKeyboard) попап **не вызывает** — ни на бутах,
  ни на bounce. Список XwaylandEisNoPromptApps на хосте (kde-translate, recwake*, python3) —
  память о GRAB-попапах старых прототипов, не о raw.
- XI2 raw на rootless XWayland структурно пуст для инжектируемых клавиш: xterm рядом
  получает SIGINT (core-доставка ✓), а xinput --root = 0 RAWKEY (send-events вычищаются
  из raw-потока). Ctrl+C+C без физического устройства на Wayland не тестится → жест = KGA-
  пара (тесты 05/07/08); XI2-покрытие — X11-джоб (debian-kde X11).
- Фикстура больше НЕ пишет XwaylandEisNoPrompt* («грант на разрешение, которого нет»);
  в conftest — измеренные факты. Сетап 8/8 зелёные на чистом kwinrc.
- В grid-инфраструктуру добавлена мышь: `vmctl.sh mouse move|click X Y [button]` +
  Grid.mouse_click/mouse_move — на день, когда придётся кликать реальный диалог (grab-
  вариант, portal-запросы). VM возвращена в чистое состояние (pure arch, без cachyos-repo).

# Найдено в текущей версии программы (к обсуждению, ничего не менял)
1. `traduku --version` печатает «traduku » — QApplication::setApplicationVersion не вызывается
   (version из CMake не проброшен). В README smoke-тест «--version» формально проходит (rc=0),
   строка версии пустая.
2. На Arch для запуска под Plasma Wayland нужен пакет **qt6-wayland** (плагин platform) —
   в depends PKGBUILD его нет: приложение крашится «no Qt platform plugin» на чистой системе.
   (На Debian это libqt6wayland6.)
3. tst_chunkedtranslator — flaky (race на порядок commit'ов; в соседнем репозитории исправлен,
   здесь на HEAD 2684afa воспроизводится под параллельной нагрузкой; standalone 3/3 зелёный;
   на debian Qt 6.8.2 подтверждён: 1 фейл из 5 прогонов). **ИСПРАВЛЕНО (2026-11-03):**
   cherry-pick d28930e (neighbor 7d37663) — assert display-row SET, not completion order.
   Гейтdebian больше не оборачивает --repeat until-pass:3 (строгий, 20/20). Стресс:
   host x20 параллель + debian-контейнер x20 параллель — 0 фейлов.
4. tst_llmstreamclient::requestCarriesProtocolFields — БАГ ТЕСТА (**ИСПРАВЛЕНО 2026-11-03**,
   одобрено «исправь тесты»):
   QVERIFY(raw.contains("Authorization: Bearer sekrit")) сверял РЕГИСТР байтов заголовка.
   Заголовок приходит всегда корректный, но Qt кладёт его в wire по-разному по версиям:
   6.8.2 (debian) — строчками «authorization:», 6.12 (хост) — «Authorization:». На debian
   фейлился детерминированно (5/5). Продукт не при чём — тест ассертит сериализацию Qt.
   **ФИКС:** cherry-pick ae9295b (neighbor 7f82696) — raw.toLower().contains(...) +
   тоLower() на негативном ассерте (п.94). Debian-гейт: убран ctest -E, строгий 20/20.
   Оба гейта (host Qt 6.12 + debian Qt 6.8.2) зелёные без исключений и без repeat.

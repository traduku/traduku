# Traduku — test pipeline

Матрица целей: **debian / ubuntu / fedora / arch × kde / gnome**, фокус Wayland
(XWayland поддерживается приложением и тестируется как отдельное свойство сессии).

## Три уровня (не смешивать)

### Уровень 1–2: unit/интеграция, сборка, упаковка → контейнеры

`distrobox` (или `docker`) по образу на дистрибутив. Полный цикл:
build-deps → `cmake` build → `ctest` → сборка пакета (`makepkg` /
`dpkg-buildpackage`) → установка пакета → `traduku --version` + `--selftest`.
Образы пинить по digest, не по тегу `latest`.

Дёшево, поэтому матрица широкая: 4 дистрибутива × 1 DE.

### Уровень 3: GUI/E2E → QEMU-грид (наш), НЕ контейнеры и НЕ Xvfb

Инфраструктура `tools/vms/` (уже обкатана: debian/ubuntu × kde/gnome,
гейт 7/7): контейнер с qemu + KVM, NoCloud-seed, SSH, QMP.

- **виртуальная клавиатура**: QMP `input-send-event` — шлёт scancodes на
  уровень железа, т.е. физический keycode C независимо от раскладки.
  Ровно то, что проверяют тесты Ctrl+C+C.
- **скриншоты**: QMP `screendump` — снимок framebuffer-а консоли, не зависит
  от композитора, работает на нативном Wayland (в отличие от XGetImage).
- **проверки внутри гостя**: D-Bus (`org.kde.StatusNotifierWatcher` — трей),
  файлы XDG autostart, `wl-copy/wl-paste` (KDE Wayland) / `xclip` (X11).
- **рестарт ОС**: `reboot` по SSH + ожидание возврата ssh.
- **глазами**: каждая VM имеет VNC (`:5900+idx`, на хосте — контейнер в host-network):
  `./tools/vms/vmctl.sh vnc arch-kde`. Это тот же экран, который снимают QMP
  screendump и который крутят QMP-нажатия: видно жесты, окна, уведомления,
  локскрин-шумы — незаменимо при отладке теста вручную (CI не нужен).

Тесты — pytest на хосте (`tests/e2e/`), каждый тест отдельно коммитится,
каждый тест проверяется «на падение» (сценарий, где проверка обязана
упасть).

## Почему не остальные кандидаты

| Кандидат | Вердикт для нашей сетки тестов |
|---|---|
| behave+qecore-headless+dogtail | AT-SPI-ассерты отличные, но Wayland-ввод только через `gnome-ponytail-daemon` (GNOME Screen Cast / Remote Desktop API). На **KDE Plasma Wayland** — our native env — инжект ввод нечем; `qecore` завязан на GDM/GNOME-сессию. Берём как идею (accessibility-деревья, HTML-отчёты) и как кандидат для слоя GNOME позже. |
| openQA | Покрывает всё (реальная VM, reboot, needles/OpenCV), так тестируют Fedora/KDE Neon. Но: webui+worker+scheduler+база+перл+needles-поддержка — тяжёлая инфраструктура, ассерты пиксельные. Кандидат на «тест глазами» (установка, темы, первый запуск) когда матрица разрастётся. |
| distrobox | Идеален для уровней 1–2. GUI-тесты из списка невозможны: нет init/reboot, нет сессии sddm, нет трея/порталов, ввод не инжектится, DE наследуется от хоста. |

## Сокращённая матрица

| Job | Дистрибутив | DE / сессия | Смысл |
|---|---|---|---|
| build×4 | debian, ubuntu, fedora, arch | — | упаковка |
| gui-wayland | **arch** | KDE Wayland | нативная среда |
| gui-x11 | debian 13 | KDE X11 | самый старый стек, X11 как свойство сессии |
| gui-wayland2 | ubuntu LTS | KDE Wayland | «как у пользователей» |
| gui-gnome (позже) | fedora | GNOME Wayland | порталы/трей-расширение |

## Тесты уровня 3 (tests/e2e/)

1. `test_install` — установка `pacman -U` (apt на debian/ubuntu) из терминала, `--version`, selftest.
2. `test_autostart` — включение автозапуска (XDG `~/.config/autostart/traduku.desktop`); DE не запрещает → проверка файла; запрос пользователю — при первом запуске.
3. `test_autostart_reboot` — рестарт ОС → процесс приложения в сессии.
4. `test_tray` — PresenceRequest `Registered` у `org.kde.StatusNotifierWatcher` + скриншот панели.
5. `test_hotkey` — двойной Ctrl+C (QMP scancodes) → запуск перевода; в т.ч. кириллическая раскладка.
6. `test_window` — после жеста окно появилось (diff framebuffer / D-Bus).
7. `test_clipboard_capture` — приложение читает буфер DE (wl-paste-клавиатура KDE).
8. `test_clipboard_pasteback` — результат перевода записан обратно в буфер.

Остановка и доклад после каждого этапа; TDD: тест пишется падающим.

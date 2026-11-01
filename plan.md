0. [x] изучить проект (baseline: 20/20 ctest; fixed flaky tst_chunkedtranslator commit-order race)
1. [x] создать git branch -b debian
2. [x] подготовить виртуальное окружение под сетку debian, ubuntu и gnome, kde (4 VMs), актуальные стабильные версии.
   Grid (tools/vms/): debian-kde X11, debian-gnome X11 (Debian 13), ubuntu-kde Wayland, ubuntu-gnome Wayland (Ubuntu 25.10: X11 удалён из Plasma/GNOME).
3. [x] установить приложение внутри VMs, при ошибках дописать установочные скрипты или пакеты. (deb build+install+selftest green x4)
4. [x] проверить работоспособность приложения с использованием виртуальной клавиатуры и скриншотов. При необходимости исправить ошибки. (gate green 7/7 x4 VMs: debian-kde, debian-gnome, ubuntu-kde, ubuntu-gnome)

важно следовать практикам TDD

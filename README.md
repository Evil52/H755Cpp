# H755Cpp

Прошивка для NUCLEO-H755ZI-Q на STM32H755. Собираем через CMake: bare metal, CMSIS, C++26. HAL, LL и RTOS здесь нет. Основной код работает на Cortex-M7; Cortex-M4 пока выполняет маленькую парковочную программу.

## Что уже есть

- Запуск M7, индикация ошибок и heartbeat на LD1.
- Свой драйвер Ethernet MAC/DMA, управление PHY LAN8742 через MDIO и обработка потери линка.
- lwIP 2.2.1 на M7: DHCP-клиент, ARP, ping, UDP и TCP echo на порту `7777`. Используем только нужные части lwIP; короткий ARP-пакет отсекается также внутри библиотеки.
- USB Device CDC на пользовательском разъёме CN13. Его реализация и отдельная аппаратная проверка описаны в [документе по USB](docs/usb-async.md).

16 ноября 2026 года на плату записан M7 BIN из [артефакта merge-коммита `47b8c37`](https://github.com/Evil52/H755Cpp/actions/runs/43763881813). После записи прочитаны и сверены все 2 МБ Flash. На прямом кабеле с Mac прошли DHCP, 3/3 ping, echo по UDP/TCP и проверка 256 UDP-пакетов и 16 TCP-соединений. M4 и option bytes при этой записи не менялись. [Запись проверки](docs/ethernet-release-hil-2026-11-16.md).

Это образ для разработки и ревью. Восстановление соединения после вынимания кабеля проверяли на инструментированной сборке, но на этом точном Release-образе повторно не проверяли. Долгую нагрузку и холодный запуск этого образа тоже ещё не принимали. [Обзор безопасности lwIP](docs/lwip-security-review-2026-11-16.md) описывает нераскрытые находки в DHCP и ядре IP; их применимость пока неизвестна. Поэтому не подключаем этот образ к недоверенной сети и не называем его готовым для серийного изделия.

## Где что лежит

| Путь | Содержимое |
| --- | --- |
| `firmware/cm7/main.cpp` | Сборка объектов M7 и запуск приложения. Начните читать код отсюда. |
| `firmware/cm7/` и `include/h755/application/` | Плата, цикл приложения, состояние запуска, heartbeat, обработка ошибок. |
| `include/h755/ethernet/`, `firmware/cm7/ethernet/`, `src/ethernet/` | MDIO, PHY, MAC/DMA, адаптер lwIP и echo. |
| `include/h755/usb/`, `firmware/cm7/usb/`, `src/usb/` | USB CDC, асинхронные операции и адаптер к TinyUSB. |
| `firmware/cm4/park.s` | Текущий образ M4 без прикладной логики. |
| `vendor/` | Зафиксированные CMSIS, TinyUSB и lwIP; версии и хеши проверяются при сборке. |
| `cmake/`, `CMakePresets.json` | Toolchain, linker scripts, состав сборок и профили CMake. |
| `tests/`, `tests/hil/` | Host-тесты с ASan/UBSan и сценарии аппаратной проверки. |
| `tools/`, `.github/workflows/`, `security/` | Проверки, сборка артефакта, CI и правила анализа. |
| `docs/` | Архитектура, результаты аппаратных проверок и открытые ограничения. |

## Как собрать и проверить

Нужны CMake 3.30+, Ninja и Python 3. Host-тесты собираются системным C++ компилятором. Для M7 нужен закреплённый GCC 16.2 из [toolchain release](https://github.com/Evil52/H755Cpp/releases/tag/toolchain-gcc-16.2.0-cm7); системный Arm GCC вместо него не подставляем.

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug --no-tests=error
python3 -m unittest discover -s tests -p 'test_*.py'

export H755_ARM_TOOLCHAIN=/absolute/path/to/arm-gcc-16.2.0-cm7
cmake --preset cm7-ethernet-release
cmake --build --preset cm7-ethernet-release
```

Локальная сборка создаёт ELF, BIN и HEX в `build/cm7-ethernet-release/firmware/`. Команды сборки ничего не прошивают. Порядок для записи платы: зелёный PR, merge, проверенный [post-merge артефакт](docs/firmware-artifacts.md), резервная копия Flash, запись и readback.

## С чего начать ревью

1. [Границы модулей M7](docs/cm7-architecture.md) и `firmware/cm7/main.cpp`.
2. [Драйвер MAC/DMA](docs/ethernet-mac-dma.md), затем `include/h755/ethernet/mac_engine.hpp` и `lwip_network.hpp`.
3. [Проверки Ethernet](docs/ethernet-lwip.md), [безопасность lwIP](docs/lwip-security-review-2026-11-16.md) и [порядок выпуска артефакта](docs/firmware-artifacts.md).

Особенно полезно проверить границы буферов DMA и lwIP, обработку ошибок линка, запуск после сброса и применимость открытых находок lwIP. Текущие проверки и их ограничения описаны в [CI и Sonar](docs/ci-and-sonar.md). Профиль MISRA C++ и его незакрытые пункты — в [отдельном документе](docs/misra-cpp-profile.md).

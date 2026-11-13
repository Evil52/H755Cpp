# H755Cpp

Начальный проект проверки инструментария для Nucleo STM32H755:
**CMake + bare metal + CMSIS**, C++26, без HAL/LL и RTOS.

Первый компонент Ethernet на M7: [неблокирующее MDIO-чтение и CMSIS-адаптер](docs/ethernet-m7.md).
Выбрана архитектура своего драйвера + lwIP. MAC/DMA, lwIP netif, startup и
работа с реальными кадрами ещё не реализованы; текущий компонент проходит
unit/host-integration проверки и компилируется для M7.

Для первого аппаратного запуска добавлены [M7 LED + PHY ID и M4 parking](docs/m7-bringup.md):
startup, linker scripts и отдельные ELF/BIN/HEX для обоих ядер. Ethernet-кадры пока
не передаются. `build/toolchain-checks/link_only.elf` остаётся техническим тестом
линковки runtime; его нельзя записывать на плату.

## Установленный компилятор

Локальная сборка upstream GCC 16.2.0 для `arm-none-eabi` на macOS arm64:

```sh
.tools/arm-gcc-16.2.0-cm7/bin/arm-none-eabi-g++ --version
```

Этот набор выбран только для проекта. Системный Arm GCC 10.3.1 не изменён.
Профиль библиотек: Cortex-M7, Thumb, FPv5-D16, hard-float, одна конфигурация.
Для M4 необходим отдельный проверенный runtime либо multilib toolchain;
передача `-mcpu=cortex-m4` сама по себе не заменяет библиотеки M7.

Версии и команды сборки: [docs/toolchain-build.json](docs/toolchain-build.json).
Архивы GCC/Binutils проверены по опубликованным SHA256; зависимости GCC —
по SHA512 из его релиза. Для Newlib записан SHA256 загрузки с HTTPS upstream.
Опциональная Info-документация libgloss не собиралась; библиотеки и заголовки установлены.

## Проверки

Нужны CMake 3.30+, Ninja и Python 3. Для host-проверки используется установленный
Apple Clang; его режим C++26 не означает поддержку всех возможностей стандарта.

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset cm7-debug
cmake --build --preset cm7-debug

cmake --preset cm7-release
cmake --build --preset cm7-release

python3 tools/check_toolchain.py
```

Host-проверка выполняется с ASan/UBSan и extensive hardening libc++.
На GNU host используется libstdc++ с `_GLIBCXX_ASSERTIONS`. Группы MDIO:
`ctest --preset host-debug -L unit --no-tests=error` и
`ctest --preset host-debug -L integration --no-tests=error`.
Подготовленный GitHub Actions workflow выполняет host-проверки; до запуска
на GitHub его результат остаётся неподтверждённым. Target/HIL/e2e им не заменяются.
Arm-проверки используют C++26 без standard decay, `-fcontracts`,
`-fcontract-evaluation-semantic=enforce`, `_GLIBCXX_ASSERTIONS=1` и явные ABI-флаги.
В Debug и Release проверяется компиляция `pre`, `post`, `contract_assert`, array/span.
`compile_commands.json` создаётся в соответствующем build-каталоге.

`tools/check_toolchain.py` дополнительно проверяет версию/target, feature macros,
ARM attributes, символы contract handler и assert, а также линковку C++ runtime.
Результаты сохраняются в `build/toolchain-checks/`. Проверки ничего не прошивают.

## Граница подтверждённого

- Поддержка синтаксиса и заголовка контрактов GCC 16.2 подтверждается отдельными пробами.
- `_GLIBCXX_ASSERTIONS` включает проверки ряда операций, но не доказывает полное
  соответствие C++26 library hardening. В этой сборке проверенные feature macros
  `__cpp_lib_hardened_span`/`__cpp_lib_hardened_array` не объявлены.
- Host-тест не проверяет native contracts GCC и поведение ARM runtime на устройстве.
- Действие обработчика при нарушении контракта, безопасные выходы, старт обоих ядер,
  кеш/DMA и физическое поведение платы ещё не проверены.
- Ни production-готовность, ни полноценная прошивка этим каркасом не заявляются.

## Дальше

Подтвердить точную модель/ревизию платы и MCU, питание/boot-состояние M4, затем
подготовить pinned CMSIS, startup/linker layout и одно наблюдаемое упражнение.
Новые комментарии в коде нужны только для критичных или неочевидных причин и ограничений.

Документы для следующего этапа:

- [NUCLEO-H755ZI-Q](https://www.st.com/en/evaluation-tools/nucleo-h755zi-q.html)
- [UM2408: плата MB1363](https://www.st.com/resource/en/user_manual/dm00499171-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf)
- [RM0399: STM32H745/755/747/757](https://www.st.com/resource/en/reference_manual/dm00176879-stm32h745-755-and-stm32h747-757-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
- [AN5557: архитектура двух ядер](https://www.st.com/resource/en/application_note/dm00733995-stm32h745755-and-stm32h747757-lines-dualcore-architecture-stmicroelectronics.pdf)
- [AN5617: межъядерное взаимодействие](https://www.st.com/resource/en/application_note/an5617-introduction-to-interprocessor-communications-for-stm32h745755-and-stm32h747757-mcus-stmicroelectronics.pdf)

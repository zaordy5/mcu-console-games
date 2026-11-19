# MCU Console Games

Тетрис, Сапёр и одиночный пинг-понг работают в одной прошивке для STM32F407. Игрок отправляет клавиши через USART2, а прошивка рисует экран управляющими последовательностями ANSI. Сборка и игры проверены на модели STM32F4 Discovery в Renode 1.17.0.

![Главное меню сборника в терминале USART2, 80 × 24](docs/screenshots/menu-usart2.png)

В главном меню нажмите `1–3`, чтобы выбрать игру. На следующем экране `1` выбирает Easy, `2` — Normal, `3` — Hard. Клавиша `Q` возвращает в меню. Для выбора нажимать Enter не нужно.

| Игра | Управление | Easy / Normal / Hard | Что меняется с уровнем |
| --- | --- | --- | --- |
| [Тетрис](docs/TETRIS.md) | Стрелки или WASD, пробел для мгновенного сброса | Падение раз в 850 / 600 / 350 мс на первом уровне | Каждые пять очищенных строк сокращают интервал падения |
| [Сапёр](docs/minesweeper.md) | Стрелки или WASD, пробел или Enter открывает клетку, `F` ставит флажок | 7 / 11 / 16 мин на поле 8 × 8 | После победы `N` переводит на поле 10 × 10, затем 12 × 12 |
| [Пинг-понг](docs/PONG.md) | `W` и `S` двигают ракетку, матч идёт до пяти очков | Компьютер двигается раз в 400 / 240 / 175 мс | После победы `N` начинает матч с более быстрым мячом |

## Сборка и запуск

Понадобятся CMake 3.16+, Arm GNU Toolchain с `arm-none-eabi-gcc` в PATH и генератор сборки GNU Make или Ninja. Команды выполняются из корня репозитория.

```sh
git rev-parse HEAD
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake
cmake --build build --verbose
```

Запустите Renode 1.17.0 и введите команды в Monitor. Замените путь на абсолютный путь к своей копии.

```text
set bin @C:/mcu-games/build/games.elf
include @C:/mcu-games/emulation/stm32f4.resc
start
```

В окне USART2 нужен терминал с ANSI RGB и областью 80 × 24 символа. Установка инструментов, запуск из архива CI, автопроверки и Doxygen описаны в [инструкции по сборке](docs/INTEGRATION.md).

## Устройство проекта

| Файлы | Назначение |
| --- | --- |
| [src/platform.c](src/platform.c), [src/startup.S](src/startup.S), [src/stm32f407.ld](src/stm32f407.ld) | USART2, SysTick, запуск Cortex-M4 и размещение секций ELF |
| [src/menu.c](src/menu.c), [include/game.h](include/game.h) | Выбор игры и сложности, общий интерфейс ввода, времени и отрисовки |
| [src/](src/) | Игровая механика и вывод в терминал |
| [tests/](tests/) | Проверки C-модулей на ПК и автосценарии USART2 в Renode |

## Материалы для проверки

- По каждому критерию указаны способ проверки, доказательство и статус. [Матрица критериев](docs/CRITERIA_AUDIT.md)
- В описи указано, какие файлы передать и как проверить их ревизию. [Комплект сдачи](docs/SUBMISSION.md) · [Отчёт QA](docs/QA_REPORT.md)
- Демонстрация относится к работе в Renode. Статус запуска на плате и SWD/JTAG ведётся отдельно. [Сценарий записи](docs/VIDEO.md) · [Задача №4](https://github.com/zaordy5/mcu-console-games/issues/4)

## Источники

**Микроконтроллер и инструменты**

1. STMicroelectronics. *STM32F405xx, STM32F407xx*. Datasheet DS8626, rev. 10. Карта выводов и интерфейсы микроконтроллера. [PDF](https://www.st.com/resource/en/datasheet/stm32f407ig.pdf)
2. STMicroelectronics. *STM32F405/415, STM32F407/417, STM32F427/437 and STM32F429/439 advanced Arm-based 32-bit MCUs*. Reference manual RM0090, rev. 21. Регистры RCC, GPIO и USART. [PDF](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
3. Yiu, J. *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors*. 3rd ed., Newnes. ISBN 978-0-12-408082-9. Запуск ядра, исключения и SysTick. [Страница издателя](https://shop.elsevier.com/books/the-definitive-guide-to-arm-cortex-m3-and-cortex-m4-processors/yiu/978-0-12-408082-9)
4. Dickey, T. E. *XTerm Control Sequences*. Справочник по позиционированию курсора и последовательностям SGR для RGB-цвета. [Справочник](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
5. Antmicro. *Renode documentation: Debugging with GDB*. Подключение отладчика к виртуальной плате. [Руководство](https://renode.readthedocs.io/en/latest/debugging/gdb.html)

**Статьи о моделях игр**

6. Demaine, E. D., Hohenberger, S., Liben-Nowell, D. “Tetris is Hard, Even to Approximate”. *COCOON 2003*, pp. 351–363. Описание модели поля, тетромино и удаления рядов. [Публикация](https://erikdemaine.org/papers/Tetris_COCOON2003/)
7. Kaye, R. “Minesweeper is NP-complete”. *The Mathematical Intelligencer*, 22(2), 9–15, 2000. Формальная модель ограничений по открытым подсказкам. [DOI 10.1007/BF03025367](https://doi.org/10.1007/BF03025367)

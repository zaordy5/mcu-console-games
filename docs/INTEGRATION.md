# Сборка, запуск и проверка

## Инструменты

Нужны Git, CMake 3.16+, Arm GNU Toolchain (`arm-none-eabi-gcc`, `arm-none-eabi-nm` и `arm-none-eabi-size` в `PATH`) и генератор сборки GNU Make или Ninja. Для автоматических проверок используйте Python 3.9+, для симуляции — Renode 1.17.0, для документации — Doxygen. Для отладки нужен GDB с поддержкой Arm.

На Debian или Ubuntu инструменты сборки можно установить так:

```sh
sudo apt install git cmake make gcc-arm-none-eabi libnewlib-dev python3 doxygen
```

Для Windows установите Arm GNU Toolchain, CMake, Ninja и Python, затем проверьте их доступность из PowerShell. [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) · [Renode 1.17.0](https://github.com/renode/renode/releases/tag/v1.17.0)

## Сборка из исходников

Клонируйте репозиторий после получения доступа или откройте переданную копию. Для проверки определённой ревизии переключитесь на SHA из `commit.txt`. При сборке из ZIP возьмите SHA из описи комплекта.

```sh
git clone https://github.com/zaordy5/mcu-console-games.git
cd mcu-console-games
```

Остальные команды выполняются из корня проекта. В ZIP без истории Git пропустите `git rev-parse HEAD`.

```sh
git rev-parse HEAD
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake
cmake --build build --verbose
arm-none-eabi-size build/games.elf
```

Для Ninja добавьте `-G Ninja` на шаге конфигурации. При смене генератора используйте пустой каталог сборки. Готовая прошивка — `build/games.elf`.

## Запуск в Renode

В Monitor укажите абсолютные пути к ELF и профилю. В примере используется папка `C:/mcu-games`.

```text
set bin @C:/mcu-games/build/games.elf
include @C:/mcu-games/emulation/stm32f4.resc
start
```

В архиве CI профиль `games.resc` уже указывает на соседний `games.elf`. Распакуйте их в одну папку, выполните `include @C:/mcu-games/games.resc`, затем `start`. В путях Monitor пробелы экранируются как `\ `.

Перейдите в окно USART2. Для общего меню нужен терминал с ANSI RGB и областью 80 × 24 символа. Нажмите `1` для Тетриса, `2` для Сапёра или `3` для пинг-понга. На втором экране те же цифры выбирают Easy, Normal и Hard. `Q` отменяет выбор или завершает сеанс игры. Для восстановления партии Сапёра откройте его снова и нажмите `U` на экране настройки поля.

## Автопроверки

Из корня проекта запустите команды:

```sh
python3 tests/systick_renode.py build/games.elf
python3 tests/menu_renode.py build/games.elf
python3 tests/tetris_renode.py --elf build/games.elf --uart-log build/tetris.uart
python3 tests/pong_renode.py build/games.elf
```

В Windows замените `python3` на `py -3`. Если Renode отсутствует в `PATH`, добавьте к команде теста `--renode "C:/path/to/renode.exe"`. SysTick ищет миллисекундный счётчик через `arm-none-eabi-nm`, поэтому передавайте ELF с таблицей символов. Обычная сборка сохраняет её.

Проверка SysTick ожидает 1000 мс за виртуальную секунду. Сценарий меню за один сеанс открывает три игры, проверяет возвраты, повторный вход в Тетрис и возобновление Сапёра. Сценарии Тетриса и Pong проверяют игровые события по USART2. На Linux CI запускает тесты с пробелами в `TMPDIR`. Команды хост-тестов C приведены в [конфигурации CI](../.github/workflows/firmware.yml).

## Отладка в Renode

Для пошаговой отладки используйте ELF и исходники одной ревизии. Откройте [сценарий GDB](VIDEO.md#ролик-2-пошаговая-отладка-сапёра-в-gdb). В нём указаны команды подключения и способ проверить изменение числа закрытых безопасных клеток Сапёра.

## Документация кода

```sh
doxygen Doxyfile
```

Откройте `build/doxygen/html/index.html`. Конфигурация включает C-исходники и заголовки в `src`, `include` и `tests`, в том числе статические функции. Чтобы проверить охват комментариев, выполните в POSIX-оболочке:

```sh
{
  cat Doxyfile
  printf '\nEXTRACT_ALL = NO\nOUTPUT_DIRECTORY = build/doxygen-audit\n'
  printf 'WARN_AS_ERROR = YES\n'
} | doxygen -
```

CI сравнивает индексы полного и строгого прогонов. В артефакте `doxygen-report` лежат HTML, XML, лог и SHA исходников. Сборка, кадры и журналы USART2 находятся в `firmware-menu`. [Опись сдачи](SUBMISSION.md)

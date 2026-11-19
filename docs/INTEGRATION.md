# Сборка и проверка общего меню

Для STM32F407 нужны CMake 3.16+ и Arm GNU Toolchain с `arm-none-eabi-gcc` и Binutils в PATH. Нужен генератор сборки, например GNU Make или Ninja (для Ninja добавьте `-G Ninja` при конфигурации). На Debian/Ubuntu для сборки с Make установите `cmake make gcc-arm-none-eabi libnewlib-dev`. Для проверки в симуляторе
используйте Renode 1.17.0. В терминале USART2 установите размер не меньше
80 × 24 и включите ANSI RGB.

```sh
git rev-parse HEAD
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc.cmake
cmake --build build --verbose
```

В Renode Monitor укажите абсолютные пути к файлам.

```text
set bin @C:/mcu-games/build/games.elf
include @C:/mcu-games/emulation/stm32f4.resc
start
```

`1` открывает Тетрис, `2` — Сапёр, `3` — Пинг-понг. На втором экране
`1/2/3` выбирают Easy, Normal или Hard, а `Q` отменяет выбор. Enter не нужен.
`Q` в запущенной игре возвращает в главное меню. Для продолжения сохранённой
партии Сапёра снова откройте Сапёр и нажмите `U` на экране настройки поля.

Для автоматических проверок нужен Python 3.9 или новее. Запускайте команды
из корня репозитория. Проверке SysTick нужны `arm-none-eabi-nm` в `PATH`
и `games.elf` с сохранённой таблицей символов. Обычная сборка выше
сохраняет её. CI на Linux проверяет сценарии с пробелами в пути временного каталога после [PR №12](https://github.com/zaordy5/mcu-console-games/pull/12).

```sh
python3 tests/systick_renode.py build/games.elf
python3 tests/menu_renode.py build/games.elf
python3 tests/tetris_renode.py --elf build/games.elf --uart-log build/tetris.uart
python3 tests/pong_renode.py build/games.elf
```

В Windows команды можно запускать через `py -3`. Пример для Тетриса:

```powershell
py -3 tests/tetris_renode.py --elf build/games.elf --uart-log build/tetris.uart
```

Если Renode нет в `PATH`, добавьте к любой из команд тестирования
`--renode "C:/path/to/renode.exe"`.

Проверка SysTick сверяет 1000 тиков за секунду виртуального времени.
Общая проверка выполняет один сеанс USART2. Она открывает три игры, проверяет
перезапуск Тетриса, сохранение и продолжение Сапёра, отмену выбора сложности
и совпадение главного меню до и после каждой игры. GitHub Actions собирает ELF
из SHA, записанного в `commit.txt`, и сохраняет его вместе с логами и двумя
снимками в артефакте `firmware-menu`.

## Пошаговая отладка в Renode

После загрузки сценария в Monitor выполните `machine StartGdbServer 3333`.
Подключите GDB из Arm GNU Toolchain к тому же ELF с символами:

```text
arm-none-eabi-gdb build/games.elf
(gdb) target remote localhost:3333
(gdb) break mines_open
(gdb) monitor start
(gdb) continue
```

В USART2 выберите Сапёр, Easy и откройте клетку пробелом. На остановке выведите
`board->remaining`, выполните шаг по исходнику и сравните значение после
раскрытия клеток. Для ELF из CI GDB может потребоваться `set substitute-path`
с путём к локальным исходникам того же SHA. Сценарий записи приведён в
[задаче №4](https://github.com/zaordy5/mcu-console-games/issues/4#issuecomment-7021985069).

## Документация кода

Из корня репозитория запустите Doxygen:

```sh
doxygen Doxyfile
```

Точка входа — `build/doxygen/html/index.html`. Конфигурация обрабатывает
C-исходники и заголовки из `src`, `include` и `tests`, включая статические функции всех трёх игр. В общий справочник попадают и символы без комментариев. Для аудита
комментариев прогоните тот же конфиг с отключённым `EXTRACT_ALL`:

```sh
{ cat Doxyfile; printf '\nEXTRACT_ALL = NO\nOUTPUT_DIRECTORY = build/doxygen-audit\n'; } | doxygen -
```

Отдельный шаг CI сохраняет оба HTML-отчёта, SHA и вывод Doxygen в артефакте
`doxygen-report`. Сверьте предупреждения и описания каждой сущности перед сдачей.

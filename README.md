# Последовательный растущий вниз пул для односвязного списка

Проект реализует односвязный список `doost::List<T, Allocator>` и аллокатор на базе последовательного пула памяти, который выделяет область адресного пространства через `mmap`, размещает элементы сверху вниз и освобождает всю область одним вызовом `munmap`.

Основная цель: сравнить обычное поэлементное выделение узлов списка через `new/delete` с выделением узлов из заранее зарезервированного пула.

## Требования

- POSIX-совместимая ОС.
- CMake 3.16 или новее.
- Компилятор C++20.

Проверялось на macOS. На Linux дополнительно используются флаги `MAP_GROWSDOWN` и `PROT_GROWSDOWN`, если они доступны в системных заголовках.

## Структура проекта

```text
.
├── CMakeLists.txt
├── include/doost/
│   ├── downward_pool.hpp
│   ├── downward_pool_allocator.hpp
│   └── list.hpp
├── src/
│   └── downward_pool.cpp
└── benchmark/
    ├── pool_baseline.cpp
    ├── pool_allocator_benchmark.cpp
    └── seq_pool.cpp
```

Ключевые файлы:

- `include/doost/list.hpp` - шаблонный односвязный список `doost::List<T, Allocator>`.
- `include/doost/downward_pool.hpp` - публичный интерфейс пула `doost::DownwardPool`.
- `src/downward_pool.cpp` - реализация пула через `mmap`, `mprotect`, `munmap`.
- `include/doost/downward_pool_allocator.hpp` - STL-style аллокатор `doost::DownwardPoolAllocator<T>`.
- `benchmark/pool_baseline.cpp` - базовый бенчмарк со стандартным `new/delete`.
- `benchmark/pool_allocator_benchmark.cpp` - бенчмарк списка с пуловым аллокатором.

## Сборка

Для релиз сборки:.

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

Для отладочной сборки:

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

После сборки доступны цели:

- `doost_pool` - статическая библиотека с реализацией пула.
- `pool_baseline` - исполняемый файл базового бенчмарка.
- `pool_allocator_benchmark` - исполняемый файл бенчмарка с пуловым аллокатором.
- `doost_tests` - тесты списка, пула и аллокатора, если включен `BUILD_TESTING`.

## Тесты
Сборка и запуск:

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
```

Отдельный запуск тестового исполняемого файла:

```bash
./cmake-build-debug/doost_tests
```



## Запуск бенчмарков

По умолчанию оба бенчмарка строят список длиной `10000000`.

Базовый вариант:

```bash
./cmake-build-release/pool_baseline
```

Пуловый вариант:

```bash
./cmake-build-release/pool_allocator_benchmark
```

Количество элементов можно передать аргументом:

```bash
./cmake-build-release/pool_baseline 10000000
./cmake-build-release/pool_allocator_benchmark 10000000
```

Для быстрой проверки можно запускать меньший размер:

```bash
./cmake-build-release/pool_allocator_benchmark 100000
```

Программа выводит:

- `Time used` - пользовательское CPU-время в микросекундах.
- `Memory used` - прирост `ru_maxrss`.
- `Node storage required` - чистый размер памяти под узлы списка.
- `Pool usable storage` - размер полезной области пула, округленный до страницы.
- `Pool used storage` - фактически занятая память внутри пула.
- `Overhead` - оценка накладных расходов относительно памяти под узлы.

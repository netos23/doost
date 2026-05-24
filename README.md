# Реализация растущего вниз последовательного пула для элементов списка

Проект реализует односвязный список `doost::List<T, Allocator>` и аллокатор на базе последовательного пула памяти, который выделяет область адресного пространства через `mmap`, размещает элементы сверху вниз и освобождает всю область одним вызовом `munmap`.

Основная цель: сравнить обычное поэлементное выделение узлов списка через `new/delete` с выделением узлов из заранее зарезервированного пула.

# Неблокирующая реализация растущего вниз последовательного пула для элементов списка

На базе предыдущего задания добавлены многопоточные варианты аллокации элементов списка. Для списка длиной `10000000` в каждом из `16` потоков сравниваются 4 способа выделения узлов:

- стандартный аллокатор `new/delete`;
- глобальный последовательный пул под `std::mutex`;
- глобальный неблокирующий последовательный пул на CAS;
- локальные последовательные пулы в потоках.

Обработчик `SIGSEGV`/`SIGBUS` включается флагом `DOOST_ENABLE_SIGSEGV_HANDLER` и при попадании в guard page печатает имя переполненного пула.

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
│   ├── detail/
│   │   └── downward_pool_storage.hpp
│   ├── downward_pool.hpp
│   ├── downward_pool_allocator.hpp
│   ├── list.hpp
│   ├── mutex_downward_pool.hpp
│   └── nonblocking_downward_pool.hpp
├── src/
│   ├── downward_pool.cpp
│   ├── downward_pool_storage.cpp
│   ├── mutex_downward_pool.cpp
│   └── nonblocking_downward_pool.cpp
└── benchmark/
    ├── standard_allocator_benchmark.cpp
    ├── global_mutex_pool_benchmark.cpp
    ├── global_nonblocking_pool_benchmark.cpp
    ├── thread_local_pool_benchmark.cpp
    ├── pool_baseline.cpp
    ├── pool_allocator_benchmark.cpp
    ├── compare_benchmarks.sh
    └── compare_allocator_benchmarks.sh
```

Ключевые файлы:

- `include/doost/list.hpp` - шаблонный односвязный список `doost::List<T, Allocator>`.
- `include/doost/downward_pool.hpp` - публичный интерфейс пула `doost::DownwardPool`.
- `include/doost/detail/downward_pool_storage.hpp` - общая POSIX-разметка памяти, guard page и регистрация имени пула.
- `include/doost/mutex_downward_pool.hpp` - глобальный пул под мьютексом.
- `include/doost/nonblocking_downward_pool.hpp` - неблокирующий глобальный пул.
- `include/doost/downward_pool_allocator.hpp` - STL-style аллокатор `doost::DownwardPoolAllocator<T>`.
- `benchmark/pool_baseline.cpp` и `benchmark/pool_allocator_benchmark.cpp` - бенчмарки  растущего вниз последовательного пула для элементов списка
- `benchmark/standard_allocator_benchmark.cpp`, `benchmark/global_mutex_pool_benchmark.cpp`, `benchmark/global_nonblocking_pool_benchmark.cpp`, `benchmark/thread_local_pool_benchmark.cpp` - бенчмарки неблокирующего  растущего вниз последовательного пула для элементов списка.

## Сборка

Для релизной сборки:

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DDOOST_ENABLE_SIGSEGV_HANDLER=ON
cmake --build cmake-build-release
```

Для отладочной сборки:

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

После сборки доступны цели:

- `doost_pool` - статическая библиотека с реализацией пула.
- `standard_allocator_benchmark` - стандартный `new/delete`.
- `global_mutex_pool_benchmark` - один глобальный пул под мьютексом.
- `global_nonblocking_pool_benchmark` - один глобальный неблокирующий пул.
- `thread_local_pool_benchmark` - локальный пул на каждый поток.
- `pool_baseline` и `pool_allocator_benchmark` - однопоточные бенчмарки растущего вниз последовательного пула для элементов списка.
- `doost_tests` - тесты списка, пула и аллокатора, если включен `BUILD_TESTING`.

Опция `DOOST_ENABLE_SIGSEGV_HANDLER` включает код обработчика `SIGSEGV`/`SIGBUS`. Бенчмарки Неблокирующиго растущего вниз последовательного пула для элементов списка» устанавливают обработчик и при попадании в guard page печатают имя переполненного пула, например `global-nonblocking-pool` или `thread-local-pool`.

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



## Бенчмарки: Реализация растущего вниз последовательного пула для элементов списка

По умолчанию бенчмарки строят список длиной `10000000`.

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

Сравнение бенчмарков:

```bash
./benchmark/compare_benchmarks.sh cmake-build-release 10000000
```

## Бенчмарки: Неблокирующая реализация растущего вниз последовательного пула для элементов списка

По умолчанию бенчмарки запускают `16` потоков, каждый поток создает и освобождает список длиной `10000000`.

Отдельный запуск:

```bash
./cmake-build-release/standard_allocator_benchmark
./cmake-build-release/global_mutex_pool_benchmark
./cmake-build-release/global_nonblocking_pool_benchmark
./cmake-build-release/thread_local_pool_benchmark
```

Аргументы: `[node-count] [thread-count]`.

```bash
./cmake-build-release/global_nonblocking_pool_benchmark 10000000 16
```

Сравнение всех четырех вариантов:

```bash
./benchmark/compare_allocator_benchmarks.sh cmake-build-release 10000000 16
```

Для быстрой проверки можно запускать меньший размер:

```bash
./benchmark/compare_allocator_benchmarks.sh cmake-build-release 100000 4
```

Программа выводит:

- `Time used` - wall-clock время в микросекундах.
- `CPU time used` - пользовательское и системное CPU-время процесса.
- `Memory used` - пиковое `ru_maxrss`, не меньше прямого размера узлов/пула.
- `Node storage required` - чистый размер памяти под узлы списка.
- `Pool usable storage` - размер полезной области пула, округленный до страницы.
- `Pool used storage` - фактически занятая память внутри пула.
- `Overhead` - оценка накладных расходов относительно памяти под узлы.

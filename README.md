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

# Автоматическое освобождение строк с однобитовым счётчиком

Маленькая однопоточная библиотека `doost_string` с умным указателем `doost::String`. Указатель владеет строковым блоком и автоматически освобождает строку, когда исчезает последний владелец.


Поддерживаются:

- создание пустого умного указателя, создание из строки и копирование из другого умного указателя;
- присваивание строки, `nullptr` и другого умного указателя;
- извлечение строки через `value()`, `view()` и `c_str()`;
- печать в поток в формате `String{unique=1, value="text"}`;
- `reset`, `swap`, move-семантика, сравнение для сортировки;
- трассировка освобождения строк в debug-сборке через `String::set_debug_trace`.

# Безопасное чтение байта памяти по адресу

Библиотека `doost_safe_memory` добавляет POSIX-функцию:

```cpp
std::optional<std::uint8_t> doost::safe_read_uint8(const std::uint8_t* p) noexcept;
```

Функция возвращает значение байта для доступного адреса и `std::nullopt`, если чтение приводит к `SIGSEGV` или `SIGBUS`. Реализация временно устанавливает обработчики сигналов вокруг одного `volatile`-чтения и восстанавливает прежние обработчики перед возвратом. Многопоточный одновременный вызов не поддерживается.

# Параллельное копирование больших блоков данных

Библиотека `doost_parallel_memcpy` содержит пул потоков и функцию:

```cpp
void* doost::parallel_memcpy(void* dst, const void* src, std::size_t size);
```

Число worker-потоков задается через `doost::set_parallel_memcpy_thread_count`. При копировании диапазон делится на части, а поток приложения забирает работу из той же очереди, что и worker-потоки. При `0` worker-потоков используется обычный `std::memcpy`.

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
│   ├── nonblocking_downward_pool.hpp
│   ├── parallel_memcpy.hpp
│   ├── safe_memory.hpp
│   └── string.hpp
├── src/
│   ├── downward_pool.cpp
│   ├── downward_pool_storage.cpp
│   ├── mutex_downward_pool.cpp
│   ├── nonblocking_downward_pool.cpp
│   ├── parallel_memcpy.cpp
│   ├── safe_memory.cpp
│   └── string.cpp
├── test/
│   ├── doost_tests.cpp
│   ├── parallel_memcpy_tests.cpp
│   ├── safe_memory_tests.cpp
│   └── string_tests.cpp
└── benchmark/
    ├── standard_allocator_benchmark.cpp
    ├── global_mutex_pool_benchmark.cpp
    ├── global_nonblocking_pool_benchmark.cpp
    ├── thread_local_pool_benchmark.cpp
    ├── string_benchmark.cpp
    ├── parallel_memcpy_benchmark.cpp
    ├── pool_baseline.cpp
    ├── pool_allocator_benchmark.cpp
    ├── compare_benchmarks.sh
    └── compare_allocator_benchmarks.sh
```

Ключевые файлы по задачам:

### Растущий вниз последовательный пул

- `include/doost/list.hpp` - шаблонный односвязный список `doost::List<T, Allocator>`.
- `include/doost/downward_pool.hpp` - публичный интерфейс пула `doost::DownwardPool`.
- `include/doost/detail/downward_pool_storage.hpp` - общая POSIX-разметка памяти, guard page и регистрация имени пула.
- `include/doost/downward_pool_allocator.hpp` - STL-style аллокатор `doost::DownwardPoolAllocator<T>`.
- `src/downward_pool.cpp` и `src/downward_pool_storage.cpp` - реализация однопоточного пула и общей POSIX-разметки.
- `benchmark/pool_baseline.cpp` и `benchmark/pool_allocator_benchmark.cpp` - бенчмарки однопоточного пула для элементов списка.
- `benchmark/compare_benchmarks.sh` - сравнение базового и пулового вариантов.

### Неблокирующий пул

- `include/doost/mutex_downward_pool.hpp` - глобальный пул под мьютексом.
- `include/doost/nonblocking_downward_pool.hpp` - неблокирующий глобальный пул.
- `src/mutex_downward_pool.cpp` и `src/nonblocking_downward_pool.cpp` - реализации многопоточных вариантов пула.
- `benchmark/standard_allocator_benchmark.cpp`, `benchmark/global_mutex_pool_benchmark.cpp`, `benchmark/global_nonblocking_pool_benchmark.cpp`, `benchmark/thread_local_pool_benchmark.cpp` - бенчмарки многопоточного выделения узлов списка.
- `benchmark/compare_allocator_benchmarks.sh` - сравнение всех многопоточных вариантов.

### Автоматическое освобождение строк

- `include/doost/string.hpp` и `src/string.cpp` - однопоточный умный указатель `doost::String` для автоматического освобождения строк.
- `test/string_tests.cpp` - тесты инициализации, присваиваний, печати, трассировки освобождения и пузырьковой сортировки.
- `benchmark/string_benchmark.cpp` - отдельный бенчмарк умных указателей на строки и пузырьковой сортировки.

### Безопасное чтение байта памяти

- `include/doost/safe_memory.hpp` и `src/safe_memory.cpp` - функция `doost::safe_read_uint8`.
- `test/safe_memory_tests.cpp` - тесты чтения доступного адреса, `nullptr`, `PROT_NONE`-страницы, `munmap`-нутого адреса, восстановления после fault и сохранения `errno`.

### Параллельное копирование памяти

- `include/doost/parallel_memcpy.hpp` и `src/parallel_memcpy.cpp` - пул потоков и функция `doost::parallel_memcpy`.
- `test/parallel_memcpy_tests.cpp` - тесты корректного копирования для `0`...`8` worker-потоков, повторных запусков, изменения размера пула и нулевого размера.
- `benchmark/parallel_memcpy_benchmark.cpp` - сравнение времени копирования больших блоков для разного числа потоков пула.

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
- `doost_string` - статическая библиотека умного указателя на строку.
- `doost_safe_memory` - статическая библиотека безопасного чтения байта по адресу.
- `doost_parallel_memcpy` - статическая библиотека параллельного копирования памяти.
- `standard_allocator_benchmark` - стандартный `new/delete`.
- `global_mutex_pool_benchmark` - один глобальный пул под мьютексом.
- `global_nonblocking_pool_benchmark` - один глобальный неблокирующий пул.
- `thread_local_pool_benchmark` - локальный пул на каждый поток.
- `string_benchmark` - бенчмарк `doost::String`.
- `parallel_memcpy_benchmark` - бенчмарк `parallel_memcpy` для больших блоков.
- `pool_baseline` и `pool_allocator_benchmark` - однопоточные бенчмарки растущего вниз последовательного пула для элементов списка.
- `doost_tests` - тесты списка, пула и аллокатора, если включен `BUILD_TESTING`.
- `string_tests` - тесты умного указателя на строки, если включен `BUILD_TESTING`.
- `safe_memory_tests` - тесты `safe_read_uint8`, если включен `BUILD_TESTING`.
- `parallel_memcpy_tests` - тесты `parallel_memcpy`, если включен `BUILD_TESTING`.

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
./cmake-build-debug/string_tests
./cmake-build-debug/safe_memory_tests
./cmake-build-debug/parallel_memcpy_tests
```

`string_tests` проверяет разнообразные инициализации, присваивания строк и умных указателей, извлечение строк, печать, debug-трассировку освобождения и пузырьковую сортировку массива `String`. Сортировка использует `swap` и проверяет, что биты уникальности у строковых блоков не меняются из-за перестановок.

`safe_memory_tests` проверяет, что `safe_read_uint8` возвращает байт из доступной памяти, `std::nullopt` для недоступных адресов и продолжает корректно работать после перехваченного fault.

`parallel_memcpy_tests` проверяет совпадение исходных и скопированных данных для `0`...`8` worker-потоков, повторное использование пула, изменение числа потоков и копирование нулевого размера.

## Бенчмарк: Параллельное копирование памяти

По умолчанию бенчмарк копирует `256` МиБ и сравнивает режимы с `0`...`8` worker-потоками. Режим `0` использует `std::memcpy` и служит базовой точкой.

```bash
./cmake-build-release/parallel_memcpy_benchmark
```

Аргументы: `[byte-count] [max-thread-count] [repeat-count]`.

```bash
./cmake-build-release/parallel_memcpy_benchmark 268435456 8 1
```

Для быстрой проверки можно запускать меньший размер:

```bash
./cmake-build-release/parallel_memcpy_benchmark 1048576 2 1
```

Программа для каждого числа потоков проверяет совпадение исходных и скопированных данных и выводит лучшее время копирования в микросекундах и пропускную способность в МиБ/с.

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

## Бенчмарк: Автоматическое освобождение строк с однобитовым счётчиком

По умолчанию бенчмарк создаёт `5000` умных указателей на строки, добавляет внешние алиасы для части элементов и сортирует массив пузырьком.

```bash
./cmake-build-release/string_benchmark
```

Количество строк можно передать аргументом:

```bash
./cmake-build-release/string_benchmark 5000
```

Программа выводит время, прирост `ru_maxrss`, количество строк, число внешних алиасов и количество уникальных строковых блоков до и после сортировки.

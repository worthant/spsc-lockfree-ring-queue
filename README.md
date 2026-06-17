<p align="center">
  <img width="500" alt="ring_buffer" src="https://github.com/user-attachments/assets/e545476a-b374-4cc8-aea6-36a2880f354d" />
  <h1 align="center">SPSC Ring Queue</h1>
</p>

<p align="center">
  <a aria-label="tests workflow" href="https://github.com/worthant/spsc-lockfree-ring-queue/actions/workflows/tests.yml">
    <img alt="" src="https://img.shields.io/github/actions/workflow/status/worthant/spsc-lockfree-ring-queue/tests.yml?branch=main&style=for-the-badge&label=tests&labelColor=000000&logo=githubactions&logoColor=white">
  </a>
  <a aria-label="ThreadSanitizer workflow" href="https://github.com/worthant/spsc-lockfree-ring-queue/actions/workflows/tsan.yml">
    <img alt="" src="https://img.shields.io/github/actions/workflow/status/worthant/spsc-lockfree-ring-queue/tsan.yml?branch=main&style=for-the-badge&label=TSan&labelColor=000000&logo=llvm&logoColor=white">
  </a>
  <a aria-label="AddressSanitizer + UBSan workflow" href="https://github.com/worthant/spsc-lockfree-ring-queue/actions/workflows/asan-ubsan.yml">
    <img alt="" src="https://img.shields.io/github/actions/workflow/status/worthant/spsc-lockfree-ring-queue/asan-ubsan.yml?branch=main&style=for-the-badge&label=ASan%2BUBSan&labelColor=000000&logo=llvm&logoColor=white">
  </a>
  <a aria-label="C++17" href="https://en.cppreference.com/w/cpp/17">
    <img alt="" src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&labelColor=000000&logo=cplusplus&logoColor=white">
  </a>
  <a aria-label="CMake" href="https://cmake.org/">
    <img alt="" src="https://img.shields.io/badge/CMake-%E2%89%A5_3.16-064F8C?style=for-the-badge&labelColor=000000&logo=cmake&logoColor=white">
  </a>
  <a aria-label="Catch2 v3" href="https://github.com/catchorg/Catch2">
    <img alt="" src="https://img.shields.io/badge/Catch2-v3-22D3EE?style=for-the-badge&labelColor=000000&logoColor=white">
  </a>
</p>

Lock-free **single-producer / single-consumer** циклическая очередь на C++17  
Статическое хранилище, без примитивов синхронизации, ручное управление временем
жизни объектов.

> [!NOTE]  
> Полный текст тестового задания — в [`TASK.md`](TASK.md).

---

## Сборка и запуск тестов

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
# или напрямую:
./build/tests/unit_tests
```

> [!IMPORTANT]  
> Зависимости тянутся автоматически (Catch2 v3 через `FetchContent`).  
> Требуется `CMake ≥ 3.16` и `компилятор C++17`.

---

## Проверка под санитайзерами

Корректность синхронизации проверяется не на словах, а прогоном под
**ThreadSanitizer**. TSan отслеживает happens-before по модели памяти C++ -
через сами атомарные операции, не через железо, => поэтому пропущенный
`acquire`/`release` он засечёт как гонку даже на сильно-упорядоченном x86. То
есть x86-раннер в CI полноценно валидирует синхронизацию, релевантную и для
слабых моделей памяти (ARM).

Конкурентный стресс-тест спрятан за опцией `RING_RUN_ALL_TESTS` (по умолчанию
выключена, чтобы обычный `ctest` оставался быстрым). Под TSan её включаем:

```bash
cmake -S . -B build-tsan \
  -DRING_RUN_ALL_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
cmake --build build-tsan --target ring_queue_tests -j
./build-tsan/ring_queue_tests             # вся сюита под TSan
./build-tsan/ring_queue_tests "[stage_6]" # или только конкурентный стейдж
```

ASan + UBSan (память, утечки, UB) — тем же приёмом:

```bash
cmake -S . -B build-asan \
  -DRING_RUN_ALL_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1"
cmake --build build-asan --target ring_queue_tests -j
./build-asan/ring_queue_tests
```

---

## Разбор ТЗ => мои инженерные решения

#### 1. «безопасную для … двух потоков (один читает, другой пишет)»

=> Это **SPSC** (single-producer / single-consumer).

Конкуренции как таковой нет, простая модель синхронизации: **продюсер владеет
`tail_`, консьюмер владеет `head_`**. Каждый поток пишет только свой счётчик и
лишь читает чужой - гонки за один и тот же счётчик нет и быть не может (<ins>by
design</ins>).

#### 2. «не использующую механизмы взаимной блокировки потоков»

=> Никаких примитивов (mutex, futex, spinlock, e.t.c.).  
=> Синхронизация - выполняется атомарно (**release / acquire**)

- producer публикует данные, затем `tail_` (release);
- consumer читает `tail_` (acquire), затем данные.

#### 3. «Память … аллоцируется статически внутри очереди»

=> Член класса `alignas(T) unsigned char storage_[sizeof(T) * CAPACITY]`.
Никакой кучи, никакого `new[]`. **`alignas(T)`**, а не зашитые 8 байт —
выравнивание берётся из реального требования типа (`alignof(T)`), иначе
over-aligned типы (например, SIMD) получили бы UB.

#### 4. «Два шаблонных параметра: `T`, `CAPACITY`»

=> `template <typename T, std::size_t CAPACITY>`;

note: `CAPACITY` - non-type template parameter. Хранится **ровно `CAPACITY`**
элементов.

#### 5. «от `T` нужны только конструктор копирования и оператор присваивания»

=> **нельзя `T storage_[CAPACITY]`** - это потребовало бы default-конструктор,
которого у `T` может не быть. Отсюда:

- сырое хранилище + **placement new** на запись;
- **copy-assignment** на извлечение (`out = stored`);
- API `try_pop(T& out)` вместо `T pop()` / `std::optional<T>` — чтобы не
  требовать от `T` ни default-конструктора, ни move-семантики.

#### 6. «Очередь поддерживает конструктор копирования и оператор присваивания»

=>
<ins>[Rule of three](https://www.google.com/url?sa=t&source=web&rct=j&opi=89978449&url=https://en.cppreference.com/cpp/language/rule_of_three&ved=2ahUKEwi4vI6b-YmVAxXtR1UIHXRWHGgQFnoECA0QAQ&usg=AOvVaw1DMB3F5zHSEHsrnpvqOeYM)</ins>:
дефолтные версии скопировали бы сырые байты, не вызвав конструктор копирования
`T`. Поэтому копирование — поэлементное, через placement new по живым элементам
источника.

#### 7. «хранятся до затирания; уничтожаются сразу после извлечения»

=> Ручное управление временем жизни:

- запись: placement new по "сырому" адресу;
- извлечение: копия в `out`, затем **сразу** явный деструктор `~T()`;
- деструктор всей очереди добивает оставшиеся объекты.

---

## Принятые решения (decisions)

> то где ТЗ допускает вариативность. Выбор сделан осознанно и зафиксирован.

#### 1. Индексы - монотонные счётчики.

Индексы только растут; физический слот = `index % CAPACITY`.

Это даёт **ровно `CAPACITY`** полезных слотов (без классической "жертвы одного
слота").

Переполнение `size_t` при 1 операции/нс наступает через ~585 лет —
задокументировано, не обрабатывается.

#### 2. Memory ordering - release/acquire

Выбираю минимально достаточный порядок для SPSC - **<ins>release&acquire</ins>**
(пояснения):

- Дефолтный `sequential consistency` не использую т.к. <ins>избыточен</ins> для
  SPSC, и на уровне железа использует тяжелые инструкции `DSB` (ARM), полностью
  останавливающие конвейер процессора. Нам гарантии total single order не нужны,
  потому что писатель и читатель общаются строго через пары своих индексов, и
  синхронизация нужна только между двумя конкретными потоками.
- Взаимодействие между ядрами цпу строится на парной связке барьеров И `release`
  (в потоке записи) И `acquire` (в потоке чтения). На уровне железа
  транслируются в легковесные `DMB` (ARM)

> [!IMPORTANT]  
> DMB - Data Memory Barrier. Легковесные <ins>в сравнении</ins> с DSB - Data
> Synchronization Barrier, которые генерит `seq_cst` барьер.  
> DMB упорядочивают доступ к памяти без полной заморозки конвейера вычислений.

- В местах, где отсутствует межпоточная конкуренция (например, в конструкторе
  копирования, работающем в одном потоке, или при чтении собственного индекса
  внутри владеющего им потока), используется порядок **`relaxed`**,
  гарантирующий только атомарность самой переменной

##### Как работает, почему нужны rel и acq:

1. release - на операции `m_tail.store()` (в потоке Писателя) ставит барьер
   ПЕРЕД записью индекса, принудительно заставляя цпу завершить копирование байт
   объекта и вытолкнуть их из кеша в RAM - ДО ТОГО, как обновится индекс.

Нужно это чтобы предотвратить проблему из-за механизма OoO (out of order
execution), при которой цпу переставит инструкции местами для внутренней
оптимизации конвейера вычислений и обновит индекс `m_tail` ДО ТОГО, как
физически допишет байты объекта в память.

```
=> читатель: "о, индекс обновился, прочту-ка недописанные байты из памяти"
=> segmentation fault
```

2. acquire - для `m_tail.load()` (в потоке Читателя) ставит барьер ПОСЛЕ чтения
   индекса чтоб запретить цпу спекулятивное чтение данных. Гарантия того что
   читатель пойдёт в RAM за объектом _СТРОГО после того, как атомарно подтвердит
   увеличение индекса хвоста_.

Тут мы предотвращаем механизм **спекулятивного чтения**, при котором цпу
попытается заглянуть "вперёд" и заранее закешировать данные из памяти, чтоб
конвейер не простаивал. А иначе:

```
=> читатель: на своём ядре заранее считывает данные из ячейки буфера в L1 (где ещё мусор)
=> писатель: на другом ядре делает push, обновляет индекс m_tail
=> читатель: "о, индекс обновился, данные есть, прочту-ка из своего старого кеша, куда я залез спекулятивно, мусор"
(индекс m_tail изменился, но и байты объекта тоже изменились. а мы читаем старые из ячейки, которые выгрузили спекулятивно)
```

Таким образом нам нужны и acquire и release и relaxed, в зависимости от
ситуации.

---

## Roadmap (инкрементальная сборка)

- [x] **Инкремент 0** — каркас: CMake, Catch2, структура репозитория.
- [ ] **Инкремент 1** — сырое хранилище + время жизни, однопоточно
      (`try_push`/`try_pop`). Тесты: `tests/test_single_thread.cpp`.
- [ ] **Инкремент 2** — границы: full/empty, обмотка, политика заполнения.
- [ ] **Инкремент 3** — конкурентность: атомарные индексы, release/acquire,
      стресс-тест двух потоков, прогон под ThreadSanitizer.
- [ ] **Инкремент 4** — копи-конструктор и оператор присваивания (rule of
      three).
- [ ] **Инкремент 5** — полировка: диаграмма, бенчмарк, заметка про false
      sharing / padding, интерактивная визуализация.

---

## Структура

```
include/spsc/ring_queue.hpp   — очередь (header-only)
tests/lifetime_tracker.hpp    — тип T по контракту ТЗ (без default-ctor, со счётчиками)
tests/test_single_thread.cpp  — тесты Инкремента 1
docs/tz.md                    — исходное ТЗ
```

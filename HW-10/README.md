## Домашнее задание

Модуль ядра Linux «Пул сообщений на kmem\_cache и mempool»

Цель:

реализовать модуль ядра Linux с пулом сообщений на основе kmem\_cache и mempool, обеспечивающий асинхронную обработку очереди через таймер и управление через модульные параметры.

Описание/Пошаговая инструкция выполнения домашнего задания:

Разработать модуль ядра Linux, реализующий **однонаправленную очередь сообщений** на базе двух механизмов выделения памяти: `kmem_cache` (slab-аллокатор) и `mempool_t` (пул с гарантированным резервом). Сообщения записываются пользователем через `module_param_cb`; потребитель-таймер периодически извлекает их из очереди, выводит в `dmesg` и освобождает память.

---

## Архитектура модуля

```
  Пользователь (userspace)
        │
        │  echo "hello" > /sys/module/.../parameters/send
        ▼
  ┌─────────────────────────────────────────────┐
  │           module_param_cb (send)            │
  │  1. Выделить struct msg из kmem_cache/pool  │
  │  2. Скопировать текст в msg->text           │
  │  3. Поместить в кольцевую очередь (FIFO)    │
  └───────────────────┬─────────────────────────┘
                      │ очередь защищена spinlock
  ┌───────────────────▼─────────────────────────┐
  │           Потребитель (kernel timer)         │
  │  Каждые interval_ms мс:                     │
  │  1. Извлечь все накопленные сообщения        │
  │  2. Вывести каждое в dmesg (pr_info)         │
  │  3. Освободить объект в kmem_cache/pool      │
  │  4. Перезапустить таймер                    │
  └─────────────────────────────────────────────┘
        │
        │  echo 1 > /sys/module/.../parameters/flush
        ▼
  Принудительно освободить все объекты в очереди
```

---

## Требования к реализации

### 1. Параметры модуля


| Параметр | Тип | По умолчанию | Описание                                                                                                                  |
| :--------------- | :----- | :---------------------- | :-------------------------------------------------------------------------------------------------------------------------------- |
| `alloc_type`     | uint   | 0                       | Тип аллокатора:`0` — kmem\_cache, `1` — mempool                                                                    |
| `pool_min_nr`    | uint   | 8                       | Минимальный резерв объектов в mempool (используется при`alloc_type=1`)                   |
| `interval_ms`    | uint   | 1000                    | Период срабатывания потребителя-таймера (мс)                                                |
| `send`           | write  | —                      | Отправить сообщение в очередь                                                                           |
| `inbox`          | read   | —                      | Прочитать последнее обработанное сообщение (или "empty")                                |
| `stats`          | read   | —                      | Статистика: отправлено / обработано / сброшено / текущий размер очереди |
| `flush`          | write  | —                      | Принудительно освободить все сообщения из очереди                                     |

Ограничения:

* `alloc_type` — только `0` или `1`.
* `pool_min_nr >= 1` и `pool_min_nr <= 64`.
* `interval_ms >= 100` и `interval_ms <= 60000`.
* Максимальный размер очереди — `MSG_QUEUE_MAX = 16` сообщений. При попытке добавить 17-е — вернуть `-ENOBUFS`.
* Максимальная длина текста одного сообщения — `MSG_TEXT_MAX = 128` байт (включая `\0`).

---

### 2. Структуры данных

#### 2.1 Объект сообщения

```c
#define MSG_TEXT_MAX  128

struct msg {
    char          text[MSG_TEXT_MAX]; /* текст сообщения                  */
    ktime_t       enqueue_time;       /* момент постановки в очередь      */
    unsigned int  seq;                /* порядковый номер сообщения        */
};
```

#### 2.2 Кольцевая очередь

```c
#define MSG_QUEUE_MAX 16

struct msg_queue {
    struct msg   *slots[MSG_QUEUE_MAX]; /* слоты очереди (кольцевой буфер) */
    unsigned int  head;                 /* индекс чтения                   */
    unsigned int  tail;                 /* индекс записи                   */
    unsigned int  count;                /* текущее число элементов         */
    spinlock_t    lock;                 /* защита head/tail/count          */
};
```

#### 2.3 Глобальный контекст модуля

```c
struct msgpool_ctx {
    unsigned int      alloc_type;
    unsigned int      pool_min_nr;
    unsigned int      interval_ms;

    struct kmem_cache *msg_cache;    /* slab-кеш объектов struct msg       */
    mempool_t         *msg_pool;     /* пул поверх msg_cache (alloc_type=1) */

    struct msg_queue  queue;

    struct timer_list consumer_timer;

    /* Статистика */
    atomic_t  sent_total;            /* всего поставлено в очередь         */
    atomic_t  consumed_total;        /* всего обработано таймером          */
    atomic_t  flushed_total;         /* всего сброшено через flush         */
    atomic_t  dropped_total;         /* отброшено (очередь была полна)     */

    /* Последнее обработанное сообщение (для параметра inbox) */
    char      last_msg[MSG_TEXT_MAX];
    spinlock_t last_msg_lock;

    unsigned int seq_counter;        /* монотонный счётчик сообщений       */
};
```

---

### 3. Логика компонентов

#### 3.1 Отправка сообщения (параметр `send`)

При записи строки в параметр `send` модуль должен:

1. Проверить длину строки (`<= MSG_TEXT_MAX - 1`), иначе обрезать с предупреждением.
2. Попытаться выделить объект `struct msg` из аллокатора:
   * `alloc_type == 0`: `kmem_cache_alloc(ctx->msg_cache, GFP_KERNEL)`
   * `alloc_type == 1`: `mempool_alloc(ctx->msg_pool, GFP_KERNEL)`
3. Если аллокация не удалась (`NULL`) — вернуть `-ENOMEM`.
4. Инициализировать объект: скопировать текст, зафиксировать `enqueue_time = ktime_get()`, задать `seq`.
5. Захватить `queue.lock`, проверить `count < MSG_QUEUE_MAX`:
   * Если очередь полна — освободить объект, увеличить `dropped_total`, вернуть `-ENOBUFS`.
   * Иначе: записать в `slots[tail]`, обновить `tail = (tail + 1) % MSG_QUEUE_MAX`, увеличить `count`.
6. Освободить `queue.lock`, увеличить `sent_total`.

#### 3.2 Потребитель-таймер

Таймер создаётся в `module_init` через `timer_setup` и запускается вызовом `mod_timer`. Колбэк таймера должен:

1. Захватить `queue.lock`.
2. Извлечь **все** накопленные сообщения (цикл пока `count > 0`):
   * Взять `slots[head]`, обновить `head = (head + 1) % MSG_QUEUE_MAX`, уменьшить `count`.
3. Освободить `queue.lock`.
4. Для каждого извлечённого объекта:
   * Вывести в `dmesg`: `pr_info("msgpool: [%u] %s (queued %lld ns ago)\n", seq, text, elapsed_ns)`.
   * Сохранить текст в `ctx->last_msg` (под защитой `last_msg_lock`).
   * Освободить объект:
     * `alloc_type == 0`: `kmem_cache_free(ctx->msg_cache, obj)`
     * `alloc_type == 1`: `mempool_free(obj, ctx->msg_pool)`
   * Увеличить `consumed_total`.
5. Перезапустить таймер: `mod_timer(&ctx->consumer_timer, jiffies + msecs_to_jiffies(ctx->interval_ms))`.

> **Важно:** в колбэке таймера нельзя спать. Освобождение через `kmem_cache_free` и `mempool_free` с уже выделенными объектами — атомарная операция, это допустимо.

#### 3.3 Принудительный сброс (параметр `flush`)

При записи ненулевого значения в `flush`:

1. Захватить `queue.lock`, извлечь все элементы из очереди.
2. Освободить `queue.lock`.
3. Для каждого объекта: освободить память (без вывода в dmesg), увеличить `flushed_total`.

---

## Интерфейс module\_param\_cb

### `send` (write-only, режим 0200)

```bash
echo "Hello from userspace" > /sys/module/kernel_msgpool/parameters/send
# dmesg: msgpool: Hello from userspace (queued 1002345678 ns ago)
```

При попытке отправить в полную очередь:

```bash
# Если очередь заполнена (16 сообщений не обработано):
echo "overflow" > /sys/module/kernel_msgpool/parameters/send
# bash: echo: write error: No buffer space available
```

### `inbox` (read-only, режим 0444)

```bash
cat /sys/module/kernel_msgpool/parameters/inbox
# Hello from userspace
```

Возвращает текст последнего обработанного таймером сообщения. Если ни одного сообщения ещё не обработано — возвращает строку `"(empty)"`.

### `stats` (read-only, режим 0444)

```bash
cat /sys/module/kernel_msgpool/parameters/stats
# sent=5 consumed=3 flushed=0 dropped=0 queued=2 alloc=kmem_cache interval_ms=1000
```

### `flush` (write-only, режим 0200)

```bash
echo 1 > /sys/module/kernel_msgpool/parameters/flush
# dmesg: msgpool: flushed 2 message(s)
```

### `alloc_type` (read-write, режим 0644)

Изменение `alloc_type` допустимо только при **пустой очереди**. Если в очереди есть необработанные сообщения — возвращать `-EBUSY`.

```bash
echo 1 > /sys/module/kernel_msgpool/parameters/alloc_type  # переключить на mempool
```

### `interval_ms` (read-write, режим 0644)

При записи нового значения таймер перезапускается с новым периодом немедленно:

```bash
echo 500 > /sys/module/kernel_msgpool/parameters/interval_ms
```

---

## Требования к безопасности и корректности

* `kmem_cache_destroy()` вызывать **только** после того, как все объекты из кеша освобождены; перед этим обязательно вызвать `flush`, затем `del_timer_sync()`.
* При `alloc_type == 1` уничтожать ресурсы строго в порядке: `del_timer_sync` → `flush` → `mempool_destroy` → `kmem_cache_destroy`.
* Не вызывать функции, способные **спать** (`mempool_alloc` с `GFP_KERNEL`, `kmem_cache_alloc` с `GFP_KERNEL`), из колбэка таймера. В таймере допустимо только освобождение (`kmem_cache_free`, `mempool_free`).
* Все обращения к полям `queue` (head, tail, count, slots) защищать `spinlock`.
* Валидировать все входные данные через `kstrtouint()` и `strscpy()`.

---

## Рекомендуемые коды возврата

```c
#define MP_OK        0           /* операция успешна             */
#define MP_INVALID  (-EINVAL)    /* неверный параметр            */
#define MP_NOMEM    (-ENOMEM)    /* недостаточно памяти          */
#define MP_BUSY     (-EBUSY)     /* операция недоступна сейчас   */
#define MP_FULL     (-ENOBUFS)   /* очередь переполнена          */
```

---

## Структура проекта

```
kernel_msgpool_module/
├── Makefile
├── Kbuild
└── src/
    ├── main.c      # module_init/exit, инициализация контекста, таймер
    ├── params.c    # реализация module_param_cb интерфейса
    ├── queue.c     # операции с кольцевой очередью (enqueue/dequeue/flush)
    └── alloc.c     # обёртки msg_alloc() / msg_free()
```

Модуль должен иметь название `kernel_msgpool.ko` при сборке.

---

## Пример полного сеанса работы

```bash
# Загрузка модуля: kmem_cache, таймер каждые 2 секунды
sudo insmod kernel_msgpool.ko alloc_type=0 interval_ms=2000

# Отправка нескольких сообщений
echo "message one"   > /sys/module/kernel_msgpool/parameters/send
echo "message two"   > /sys/module/kernel_msgpool/parameters/send
echo "message three" > /sys/module/kernel_msgpool/parameters/send

# Через 2 секунды таймер сработает — в dmesg:
# msgpool: message one   (queued 2001345000 ns ago)
# msgpool: [^1] message two   (queued 2001100000 ns ago)
# msgpool: [^2] message three (queued 2000800000 ns ago)

# Прочитать последнее обработанное сообщение
cat /sys/module/kernel_msgpool/parameters/inbox
# message three

# Статистика
cat /sys/module/kernel_msgpool/parameters/stats
# sent=3 consumed=3 flushed=0 dropped=0 queued=0 alloc=kmem_cache interval_ms=2000

# Переключиться на mempool
echo 0 > /sys/module/kernel_msgpool/parameters/alloc_type  # убедиться что очередь пуста
echo 1 > /sys/module/kernel_msgpool/parameters/alloc_type

echo "mempool message" > /sys/module/kernel_msgpool/parameters/send

# Принудительный сброс (освободить не обработанные сообщения)
echo 1 > /sys/module/kernel_msgpool/parameters/flush

# Изменить период таймера
echo 500 > /sys/module/kernel_msgpool/parameters/interval_ms

# Выгрузка
sudo rmmod kernel_msgpool
```

---

## Ключевые API

```c
#include ux/slab.h>
#include ux/mempool.h>
#include ux/timer.h>
#include ux/spinlock.h>
#include ux/jiffies.h>
#include ux/string.h>

/* kmem_cache */
struct kmem_cache *cache = kmem_cache_create("msgpool_cache",
    sizeof(struct msg), 0, SLAB_HWCACHE_ALIGN, NULL);
struct msg *obj = kmem_cache_alloc(cache, GFP_KERNEL);
kmem_cache_free(cache, obj);
kmem_cache_destroy(cache);   /* только когда все объекты освобождены! */

/* mempool поверх kmem_cache */
mempool_t *pool = mempool_create_slab_pool(min_nr, cache);
struct msg *obj = mempool_alloc(pool, GFP_KERNEL); /* никогда не NULL в process context */
mempool_free(obj, pool);
mempool_destroy(pool);   /* вызвать ДО kmem_cache_destroy */

/* Таймер */
struct timer_list t;
timer_setup(&t, my_timer_callback, 0);
mod_timer(&t, jiffies + msecs_to_jiffies(1000));
del_timer_sync(&t);   /* дождаться завершения текущего колбэка и остановить таймер */

/* Spinlock */
spinlock_t lock;
spin_lock_init(&lock);
spin_lock(&lock);       /* захват (не спит, можно в таймере) */
spin_unlock(&lock);

/* Время */
ktime_t t1 = ktime_get();
s64 ns = ktime_to_ns(ktime_sub(ktime_get(), t1));

/* Безопасное копирование строки */
strscpy(dst, src, sizeof(dst));   /* обрезает, всегда ставит \0 */
```

---

## Формат сдачи

```
студент_фамилия_kernel_msgpool.tar.gz
├── Makefile
├── Kbuild
└── src/
    ├── main.c
    ├── params.c
    ├── queue.c
    └── alloc.c
```

Критерии оценки:

Статус "Принято" ставится при выполнении всех требований к реализации

Компетенции:

* Использование структур данных и алгоритмов
  * - знать и применять методы оптимизации работы со структурами данных, включая подходы к минимизации времени выполнения и использования памяти
* Работа с памятью
  * - уметь применять ""mempool"" и оптимизировать размеры пулов

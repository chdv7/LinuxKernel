## Домашнее задание Модуль ядра Linux «Spinlock, Mutex, Semaphore»

Цель:
реализовать многопоточный доступ к общему счётчику с разными типами блокировок и сбором статистики ожиданий.

Описание/Пошаговая инструкция выполнения домашнего задания:
Разработать модуль ядра Linux, который демонстрирует работу трёх примитивов синхронизации, управляет общим разделяемым счётчиком и позволяет выбирать тип блокировки, инкрементировать/декрементировать счётчик из нескольких параллельных kthread, а также собирать статистику времени ожидания.

Требования к реализации

1. Параметры модуля

Модуль должен принимать параметры при загрузке:

Параметр	Тип	Значение по умолчанию	Описание
num_threads	uint	4	Количество рабочих kthread, одновременно модифицирующих счётчик
iterations	uint	1000	Количество итераций (инкремент + декремент) на каждый поток
lock_type	uint	0	Тип блокировки: 0 — spinlock, 1 — mutex, 2 — semaphore

Ограничения:

num_threads >= 1 и num_threads <= 32.
iterations >= 1 и iterations <= 1000000.
lock_type — только 0, 1 или 2.

2. Структуры данных

2.1 Глобальный контекст модуля

struct sync_ctx {
unsigned int    num_threads;
unsigned int    iterations;
unsigned int    lock_type;

long long       shared_counter;   /* защищаемый счётчик */

spinlock_t      slock;
struct mutex    mlock;
struct semaphore sem;

/* Статистика */
ktime_t         total_wait_time;  /* суммарное время ожидания блокировки */
unsigned int    contention_count; /* число раз, когда поток ждал */

/* Управление потоками */
struct task_struct **threads;
atomic_t        threads_done;
int             last_run_result;  /* 0 = OK, <0 = ошибка */
};

2.2 Аргументы рабочего потока

struct worker_args {
struct sync_ctx *ctx;
unsigned int     thread_id;
ktime_t          wait_time;   /* время ожидания конкретного потока */
};

3. Логика рабочих потоков

При запуске теста (run) модуль создаёт num_threads потоков через kthread_create(). Каждый поток выполняет следующую логику:

В цикле iterations раз:
Зафиксировать время до захвата блокировки: t_before = ktime_get();
Захватить блокировку согласно lock_type:
0: spin_lock(&ctx->slock);
1: mutex_lock(&ctx->mlock);
2: down(&ctx->sem);
Зафиксировать время после захвата: t_after = ktime_get();
Если t_after - t_before > порог (например, 100 нс) — увеличить args->wait_time и считать contention.
Выполнить: ctx->shared_counter++;
Освободить блокировку:
0: spin_unlock(&ctx->slock);
1: mutex_unlock(&ctx->mlock);
2: up(&ctx->sem);
Повторить инкремент → декремент (ctx->shared_counter--) в отдельном захвате/освобождении.
По завершении атомарно увеличить threads_done.
Вернуть 0.

Обязательное требование: каждый поток обязан сам захватывать и освобождать блокировку на каждой итерации — нельзя держать блокировку на весь цикл.

Интерфейс модульных параметров (module_param_cb)

Модуль должен поддерживать взаимодействие через module_param_cb.

Параметр	Доступ	Описание	Формат
run	write	Запустить тест с текущими параметрами	echo 1 > /sys/module/kernel_sync_demo/parameters/run
result	read	Получить итог последнего запуска	cat /sys/module/kernel_sync_demo/parameters/result
lock_type	write	Сменить тип блокировки (0/1/2)	echo 2 > /sys/module/kernel_sync_demo/parameters/lock_type
stats	read	Статистика ожиданий последнего запуска	cat /sys/module/kernel_sync_demo/parameters/stats
reset	write	Сбросить счётчик и статистику	echo 1 > /sys/module/kernel_sync_demo/parameters/reset

4.1 run
При записи 1 в параметр run модуль должен:

Проверить, что предыдущий тест не запущен (если потоки ещё работают — вернуть -EBUSY).
Инициализировать нужную блокировку:
spin_lock_init(&ctx->slock);
mutex_init(&ctx->mlock);
sema_init(&ctx->sem, 1);
Сбросить shared_counter = 0, total_wait_time = 0, contention_count = 0.
Создать num_threads потоков через kthread_create() и запустить их через wake_up_process().
Дождаться завершения всех потоков (можно через kthread_stop() или wait_event()).
Суммировать wait_time из каждого worker_args в ctx->total_wait_time.

4.2 result
При чтении возвращать однострочную строку, например:

counter=0 threads=4 iterations=1000 lock=spinlock ok
Поле counter должно быть 0 при корректной работе (инкрементов и декрементов поровну). Любое ненулевое значение указывает на race condition.

4.3 stats
При чтении возвращать:

contention=42 total_wait_ns=18500 avg_wait_ns=440

4.4 reset
При записи любого ненулевого значения:

Проверить, что тест не активен.
Обнулить shared_counter, total_wait_time, contention_count, last_run_result.

Требования к безопасности и корректности

Корректно обрабатывать ошибки kthread_create() и освобождать ресурсы при неудаче.
Не использовать mutex_lock() и down() в прерываниях или при удержании спинлока.
При lock_type = 0 (spinlock) — не вызывать функции, способные уснуть внутри критической секции.
Освобождать всю память (kfree) при выгрузке модуля (module_exit).
Валидировать все входные данные в param_cb через kstrtouint().

Рекомендуемые коды возврата

#define SD_OK           0        /* операция успешна */
#define SD_INVALID     -EINVAL   /* неверный параметр */
#define SD_NOMEM       -ENOMEM   /* недостаточно памяти */
#define SD_BUSY        -EBUSY    /* тест уже выполняется */

Структура проекта

kernel_sync_demo_module/
├── Makefile
├── Kbuild
└── src/
├── main.c      # init/exit, инициализация контекста
├── params.c    # реализация module_param_cb интерфейса
├── worker.c    # логика рабочих потоков (kthread)
└── sync.c      # обёртки захвата/освобождения блокировок

Модуль должен иметь название kernel_sync_demo.ko при сборке.

Пример использования после загрузки модуля

# Загрузка модуля

sudo insmod kernel_sync_demo.ko num_threads=8 iterations=5000 lock_type=0

# Запуск теста со spinlock

echo 1 > /sys/module/kernel_sync_demo/parameters/run

# Результат

cat /sys/module/kernel_sync_demo/parameters/result

# counter=0 threads=8 iterations=5000 lock=spinlock ok

# Статистика

cat /sys/module/kernel_sync_demo/parameters/stats

# contention=317 total_wait_ns=142000 avg_wait_ns=447

# Переключиться на mutex и запустить снова

echo 1 > /sys/module/kernel_sync_demo/parameters/lock_type
echo 1 > /sys/module/kernel_sync_demo/parameters/run

# Сброс состояния

echo 1 > /sys/module/kernel_sync_demo/parameters/reset

# Выгрузка

sudo rmmod kernel_sync_demo

Формат сдачи

Сдавайте архив со следующей структурой:

студент_фамилия_kernel_sync_demo.tar.gz
├── Makefile
├── Kbuild
└── src/
├── main.c
├── params.c
├── worker.c
└── sync.c

Дополнительные указания

Ключевые API синхронизации

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/ktime.h>

/* Spinlock */
spinlock_t lock;
spin_lock_init(&lock);
spin_lock(&lock);        /* захват */
spin_unlock(&lock);      /* освобождение */

/* Mutex */
struct mutex m;
mutex_init(&m);
mutex_lock(&m);          /* захват (может спать) */
mutex_unlock(&m);        /* освобождение */

/* Semaphore */
struct semaphore sem;
sema_init(&sem, 1);      /* инициализация (счётчик = 1) */
down(&sem);              /* захват (может спать) */
up(&sem);                /* освобождение */

/* kthread */
struct task_struct *t = kthread_create(fn, args, "name");
wake_up_process(t);

/* Время */
ktime_t t = ktime_get();
s64 ns = ktime_to_ns(ktime_sub(t2, t1));

Критерии оценки:
Статус "Принято" ставится при выполнении всех требований к реализации

Компетенции:
Создание и управление модулями ядра

- уметь разрабатывать многопоточные модули и синхронизировать доступ к общим ресурсам
  Управление процессами и потоками
- уметь применять примитивы синхронизации для решения задачи читателей-писателей
- уметь выбирать оптимальный примитив под задачу
- знать архитектурные особенности реализации различных примитивов синхронизации и их применения в контексте производительности системы

# HW: Spinlock, Mutex, Semaphore

Модуль `kernel_sync.ko` показывает многопоточный доступ к общему счётчику через
три примитива синхронизации: `spinlock`, `mutex` и `semaphore`.

Каждый рабочий поток выполняет `iterations` итераций. В каждой итерации поток
отдельно захватывает блокировку для инкремента счётчика и отдельно захватывает
блокировку для декремента счётчика. Если синхронизация работает корректно,
после завершения всех потоков итоговый счётчик равен `0`.

## Структура проекта

```text
.
├── Makefile
├── Kbuild
├── README.md
├── check.sh
└── src
    ├── kernel_sync.h
    ├── main.c
    ├── params.c
    ├── worker.c
    └── sync.c
```
## Сборка

```bash
make build
```
## Загрузка модуля

```bash
sudo insmod kernel_sync.ko num_threads=8 iterations=5000 lock_type=0
```
Параметры загрузки:


| Параметр | Значение по умолчанию | Описание                                                                      |
| ---------------- | ---------------------------------------: | ------------------------------------------------------------------------------------- |
| `num_threads`    |                                      `4` | количество рабочих`kthread`, допустимо `1..32`              |
| `iterations`     |                                   `1000` | количество итераций на поток, допустимо`1..1000000` |
| `lock_type`      |                                      `0` | `0` — spinlock, `1` — mutex, `2` — semaphore                                       |

## Запуск теста

```bash
echo 1 | sudo tee /sys/module/kernel_sync/parameters/run
```
## Результат

```bash
cat /sys/module/kernel_sync/parameters/result
```
Пример:

```text
counter=0 threads=8 iterations=5000 lock=spinlock ok
```
## Статистика ожиданий

```bash
cat /sys/module/kernel_sync/parameters/stats
```
Пример:

```text
contention=317 total_wait_ns=142000 avg_wait_ns=447
```
`contention` увеличивается, если время ожидания блокировки больше
`SYNC_WAIT_THRESHOLD_NS`, который равен `100` нс.

## Смена типа блокировки

```bash
echo 1 | sudo tee /sys/module/kernel_sync/parameters/lock_type
echo 1 | sudo tee /sys/module/kernel_sync/parameters/run
```
Значения `lock_type`:

```text
0 - spinlock
1 - mutex
2 - semaphore
```
## Сброс состояния

```bash
echo 1 | sudo tee /sys/module/kernel_sync/parameters/reset
```
## Выгрузка модуля

```bash
sudo rmmod kernel_sync
```
## Проверка

```bash
make check
```
Скрипт собирает модуль, загружает его, запускает тест для `spinlock`, `mutex` и
`semaphore`, проверяет `counter=0`, а также проверяет отклонение некорректных
значений параметров.

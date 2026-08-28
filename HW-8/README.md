## Домашнее задание Модуль ядра Linux «Producer/Consumer: Tasklet vs Workqueue»

Цель:
разработать модуль ядра Linux, реализующий паттерн producer/consumer в двух вариантах bottom-half обработки.

Описание/Пошаговая инструкция выполнения домашнего задания:
Разработать модуль ядра Linux, в котором producer эмулируется высокоточным таймером hrtimer, генерирует случайные числа и помещает их в очередь kfifo. Consumer реализуется двумя способами — через tasklet и через workqueue — и вычитывает данные из очереди, накапливая статистику.

Требования к реализации

1. Параметры модуля

Модуль принимает параметры при загрузке:

Параметр	Тип	Значение по умолчанию	Описание
fifo_size	uint	64	Размер kfifo, количество слотов, степень двойки
num_events	uint	200	Количество событий producer
interval_us	uint	1000	Интервал между событиями producer в микросекундах
consumer_type	uint	0	Тип consumer: 0 — tasklet, 1 — workqueue

Ограничения:

fifo_size >= 4 и fifo_size <= 1024, обязательно степень двойки.
num_events >= 1 и num_events <= 50000.
interval_us >= 100 и interval_us <= 1000000.
consumer_type — только 0 или 1.

2. Структуры данных

Глобальный контекст struct pc_ctx хранит параметры запуска, kfifo, hrtimer, tasklet, workqueue, статистику и состояние текущего теста.

Очередь создаётся через DECLARE_KFIFO_PTR и выделяется функцией kfifo_alloc() при загрузке модуля. При выгрузке используется kfifo_free().

3. Producer через hrtimer

Callback таймера выполняется в atomic context и не должен спать.

Для каждого события:
Проверяется количество уже сгенерированных и отброшенных событий.
Генерируется value = get_random_u32() % 1000.
Значение помещается в kfifo через kfifo_put().
Если очередь заполнена, событие немедленно учитывается в dropped.
После записи планируется выбранный bottom-half consumer.
Таймер переводится на следующий interval_us через hrtimer_forward_now().

Producer никогда не ждёт освобождения места в kfifo.

4. Consumer через tasklet

Tasklet вычитывает все доступные элементы через kfifo_get(), обновляет sum, last_value и consumed.

Tasklet не использует функции, которые могут спать. Для sum и last_value mutex не применяется, так как один tasklet не выполняется параллельно сам с собой.

5. Consumer через workqueue

Workqueue consumer также вычитывает элементы через kfifo_get(). При обновлении sum и last_value используется stats_lock.

Если очередь временно пуста, но producer ещё работает, consumer вызывает msleep(1) и продолжает ожидание. Workqueue выполняется в process context, поэтому такое ожидание допустимо.

6. Интерфейс module_param_cb

Параметр	Доступ	Описание	Формат
run	write	Запустить тест	echo 1 > /sys/module/kernel_pc/parameters/run
result	read	Результат последнего запуска	cat /sys/module/kernel_pc/parameters/result
stats	read	Статистика produced/consumed/dropped	cat /sys/module/kernel_pc/parameters/stats
consumer_type	write	Сменить consumer 0/1	echo 1 > /sys/module/kernel_pc/parameters/consumer_type
reset	write	Сбросить очередь и статистику	echo 1 > /sys/module/kernel_pc/parameters/reset

6.1 run

При записи 1:
Проверяется, что предыдущий тест не выполняется.
kfifo и статистика сбрасываются.
Инициализируется выбранный consumer.
Запускается hrtimer.
Выполнение ожидает produced + dropped >= num_events через wait_event_timeout().
После producer выполняется синхронная остановка consumer, чтобы последние элементы очереди были обработаны до формирования result.

6.2 result

Пример:

produced=497 consumed=497 dropped=3 consumer=tasklet ok

Если consumed < produced, дополнительно выводится warn: lost=N.

6.3 stats

Пример:

produced=497 consumed=497 dropped=3 sum=248312 last=612 avg=499

avg рассчитывается как sum / consumed целочисленно.

6.4 reset

При записи ненулевого значения:
Проверяется, что тест не активен.
Отменяется hrtimer.
Останавливается tasklet или workqueue.
Сбрасываются kfifo и статистика.

Требования к безопасности и корректности

kfifo выделяется через kfifo_alloc() и освобождается через kfifo_free().
kfifo_put() и kfifo_get() используются в конфигурации один producer / один consumer.
Tasklet не использует блокирующие примитивы.
Workqueue защищает sum и last_value через mutex.
Все входные параметры валидируются через kstrtouint().
При выгрузке модуля hrtimer и consumer останавливаются до освобождения kfifo.

Рекомендуемые коды возврата

#define PC_OK        0
#define PC_INVALID  -EINVAL
#define PC_NOMEM    -ENOMEM
#define PC_BUSY     -EBUSY
#define PC_TIMEOUT  -ETIMEDOUT

Структура проекта

HW-8/
├── Makefile
├── Kbuild
├── README.md
├── check.sh
└── src/
    ├── kernel_pc.h
    ├── main.c
    ├── params.c
    ├── producer.c
    └── consumer.c

Модуль собирается с названием kernel_pc.ko.

Сборка

make build

Форматирование исходников

make format

Загрузка модуля

sudo insmod kernel_pc.ko fifo_size=64 num_events=500 interval_us=500 consumer_type=0

Запуск с tasklet consumer

echo 1 > /sys/module/kernel_pc/parameters/run
cat /sys/module/kernel_pc/parameters/result
cat /sys/module/kernel_pc/parameters/stats

Переключение на workqueue

echo 1 > /sys/module/kernel_pc/parameters/reset
echo 1 > /sys/module/kernel_pc/parameters/consumer_type
echo 1 > /sys/module/kernel_pc/parameters/run
cat /sys/module/kernel_pc/parameters/result
cat /sys/module/kernel_pc/parameters/stats

Автоматическая проверка

make check

Выгрузка

sudo rmmod kernel_pc

## Домашнее задание Модули ядра использующие API таймеров

Цель:
написать модуль ядра с использованием `timer_list`.

### Описание

Модуль `kernel_timer` использует `struct timer_list`. По умолчанию callback вызывается каждые 30 секунд и выводит в kernel log:

```text
min=N: Hello, timer!
```

Таймер работает не более 5 минут. После пятой минуты callback больше не перепланируется.

Дополнительно реализовано изменение интервала через sysfs-параметр модуля `interval_ms`.

### Структура проекта

```text
HW-9/
├── Kbuild
├── Makefile
├── README.md
├── check.sh
└── src/
    └── main.c
```

### Сборка

```bash
make
```

или:

```bash
make build
```

### Загрузка

```bash
make load
```

По умолчанию интервал равен 30000 мс:

```bash
sudo insmod kernel_timer.ko interval_ms=30000
```

### Проверка сообщений

```bash
sudo dmesg -w
```

Ожидаемый вывод:

```text
kernel_timer: min=1: Hello, timer!
kernel_timer: min=1: Hello, timer!
kernel_timer: min=2: Hello, timer!
kernel_timer: min=2: Hello, timer!
...
kernel_timer: min=5: Hello, timer!
kernel_timer: min=5: Hello, timer!
```

При интервале 30 секунд получается 10 callback-вызовов: от 30-й секунды до 300-й секунды включительно. После этого таймер не перепланируется.

### Изменение интервала через sysfs

Текущее значение:

```bash
cat /sys/module/kernel_timer/parameters/interval_ms
```

Изменить, например, на 5 секунд:

```bash
echo 5000 | sudo tee /sys/module/kernel_timer/parameters/interval_ms
```

Новое значение применяется при следующем перепланировании таймера. Допустимый диапазон: 100..300000 мс.

### Автоматическая проверка

```bash
make check
```

`check.sh` загружает модуль с коротким интервалом, проверяет появление сообщений в `dmesg`, изменение `interval_ms` через sysfs и отклонение некорректного значения.

### Форматирование

```bash
make format
```

### Выгрузка

```bash
make unload
```

При выгрузке вызывается `timer_shutdown_sync()`, поэтому callback гарантированно завершён и не сможет повторно поставить таймер после начала выгрузки модуля.

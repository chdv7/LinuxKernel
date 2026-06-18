## Домашнее задание
Модуль ядра Linux с FIFO на основе встроенного буфера kfifo  

Цель:  
реализовать модуль ядра Linux с очередью FIFO на базе kfifo и управлением операциями через модульные параметры.  


Описание/Пошаговая инструкция выполнения домашнего задания:  
разработать модуль ядра Linux, который реализует структуру данных FIFO (First In, First Out) с использованием встроенного механизма кругового буфера ядра (kfifo), взаимодействие с модулем должно осуществляться через модульные параметры (module_param_cb).  


Требования к реализации  

1. Структура данных FIFO  

1.1 Основная структура FIFO  

Необходимо создать структуру для представления FIFO:  


struct fifo_device {  
    struct kfifo queue;             /* Встроенный буфер kfifo */  
    int max_size;                   /* Максимальный размер очереди */  
};  

1.2 Элемент очереди  

Каждый элемент FIFO содержит целое число:  


struct fifo_entry {  
    int data;                       /* Данные (целое число) */  
};  

2. Операции над FIFO  

Реализовать следующие функции:  


Функция	Описание	Возвращаемое значение  
fifo_init(int size)	Инициализация FIFO с заданным размером	int (0 - успех, -1 - ошибка)  
fifo_enqueue(int value)	Добавить элемент в конец очереди	int (0 - успех, -1 - ошибка)  
fifo_dequeue()	Удалить и вернуть элемент из начала очереди	int (значение или код ошибки)  
fifo_peek()	Получить значение из начала очереди без удаления	int (значение или код ошибки)  
fifo_is_empty()	Проверить пустоту очереди	int (1 - пусто, 0 - не пусто)  
fifo_is_full()	Проверить переполненность очереди	int (1 - полна, 0 - не полна)  
fifo_size()	Получить текущее количество элементов	int (количество элементов)  
fifo_available()	Получить количество свободного места	int (количество свободных ячеек)  
fifo_clear()	Очистить очередь	void  

3. Интерфейс модульных параметров  

Модуль должен поддерживать операции через модульные параметры с использованием module_param_cb:  


Параметр	Операция	Формат  
enqueue	Добавить элемент в очередь	Целое число (например: echo 42 > /sys/module/kernel_fifo/parameters/enqueue)  
dequeue	Получить и удалить элемент	Целое число из очереди или сообщение об ошибке  
peek	Получить значение начала очереди	Целое число из очереди или сообщение об ошибке  
size	Получить текущий размер очереди	Текущее количество элементов  
available	Получить свободное место	Количество свободных ячеек  
is_empty	Проверить пустоту	"1" (пусто) или "0" (не пусто)  
is_full	Проверить переполненность	"1" (полна) или "0" (не полна)  
clear	Очистить очередь	Любое значение для запуска очистки  
  
Указания к реализации module_param_cb интерфейса  

Используйте макрос module_param_cb() для создания параметров с callback-функциями.  


Структура callback-функции:  


static int param_set_enqueue(const char *val, const struct kernel_param *kp)  
{  
    int value;  
    if (kstrtoint(val, 10, &value) != 0)  
        return -EINVAL;  
    /* Логика добавления элемента */  
    return 0;  
}  
  
static int param_get_dequeue(char *buf, const struct kernel_param *kp)  
{  
    int value = fifo_dequeue();  
    if (value < 0)  
        return snprintf(buf, PAGE_SIZE, "error: %d\n", value);  
    return snprintf(buf, PAGE_SIZE, "%d\n", value);  
}  

module_param_cb(enqueue, &param_ops_charp, NULL, S_IWUSR);  
module_param_cb(dequeue, &param_ops_charp, NULL, S_IRUSR);  

4. Требования к безопасности  

Корректно обрабатывать ошибки выделения памяти (kmalloc)  
Проверять граничные условия (операции над пустой/полной очередью)  
Освобождать память при удалении модуля  
Валидировать входные данные в callback-функциях  
  
5. Обработка ошибок  
  
Определить и документировать коды возврата:  
  
  
/#define FIFO_OK          0      /* Операция успешна */  
/#define FIFO_EMPTY      -1      /* Очередь пуста */  
/#define FIFO_FULL       -2      /* Очередь полна */  
/#define FIFO_NOMEM      -3      /* Нет памяти */  
/#define FIFO_INVALID    -4      /* Неверный параметр */  
  

Структура проекта  

kernel_fifo_module/  
├── Makefile                 # Главный файл сборки  
├── Kbuild                   # Конфигурация сборки модуля  
└── src/  
    ├── main.c               # Основной код модуля  
    ├── params.c             # Реализация module_param_cb интерфейса  
    └── fifo_ops.c           # Реализация операций с FIFO  
  
Модуль должен иметь название kernel_fifo.ko при сборке.  
  
  

Пример использования после загрузки модуля  

# Загрузить модуль  
sudo insmod kernel_fifo.ko  

# Добавить элементы в очередь через параметры  
echo 10 > /sys/module/kernel_fifo/parameters/enqueue  
echo 20 > /sys/module/kernel_fifo/parameters/enqueue  
echo 30 > /sys/module/kernel_fifo/parameters/enqueue  
  
# Проверить размер очереди  
cat /sys/module/kernel_fifo/parameters/size  
# Вывод: 3  

# Получить элемент начала очереди без удаления  
cat /sys/module/kernel_fifo/parameters/peek  
# Вывод: 10  

# Удалить и получить элемент из начала очереди  
cat /sys/module/kernel_fifo/parameters/dequeue  
# Вывод: 10  

# Проверить новый размер  
cat /sys/module/kernel_fifo/parameters/size  
# Вывод: 2  

# Проверить свободное место  
cat /sys/module/kernel_fifo/parameters/available  
# Вывод: 98 (если размер буфера был инициализирован как 100)  

# Проверить пустоту  
cat /sys/module/kernel_fifo/parameters/is_empty  
# Вывод: 0  

# Проверить переполненность  
cat /sys/module/kernel_fifo/parameters/is_full  
# Вывод: 0  

# Очистить очередь  
echo 1 > /sys/module/kernel_fifo/parameters/clear  

# Выгрузить модуль  
sudo rmmod kernel_fifo  

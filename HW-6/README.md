# Домашнее задание Модуль ядра Linux «Hashtable + Binary Search»

Цель:
реализовать модуль ядра Linux, выполняющий поиск значений в хэш-таблице с сортировкой корзин и бинарным поиском через интерфейс модульных параметров.

Описание/Пошаговая инструкция выполнения домашнего задания:
Разработать модуль ядра Linux, который создаёт массив случайных uint длиной array_size, где все значения лежат в диапазоне 0..array_size-1, затем строит хэш-таблицу с использованием встроенного API DEFINE_HASHTABLE, сортирует элементы hash_entry внутри каждой корзины по полю value и выполняет поиск числа через бинарный поиск (bsearch()) с использованием module_param_cb.

Требования к реализации

1. Параметры модуля

Модуль должен принимать параметры при загрузке:

Параметр	Тип	Значение по умолчанию	Описание
array_size	uint	1024	Количество элементов массива и верхняя граница значений (значения генерируются в 0..array_size-1).

2. Структуры данных

2.1 Элемент хэш-таблицы

Каждый элемент массива хранится в структуре:

struct hash_entry {
struct hlist_node node;      /* узел для встраивания в хэш-таблицу */
unsigned int value;          /* значение элемента */
};

2.2 Контекст модуля

Модуль хранит глобальный контекст:

#define HASH_BITS_COUNT 6 /* размер хэш-таблицы */

struct bucket_search_ctx {
unsigned int array_size;

DECLARE_HASHTABLE(htable, HASH_BITS_COUNT);  /* хэш-таблица */

/* Для хранения результата последнего поиска */
int last_found;
unsigned int last_value;
unsigned int last_bucket;

/* Для bucket_dump */
unsigned int current_bucket_id;
};

Примечание: DECLARE_HASHTABLE(htable, HASH_BITS_COUNT) создаёт хэш-таблицу фиксированного размера 2^HASH_BITS_COUNT корзин.

3. Генерация данных и построение хэш-таблицы

При инициализации модуля:

Инициализировать хэш-таблицу: hash_init(ctx->htable);
Сгенерировать array_size случайных значений в диапазоне 0..array_size-1.
Для каждого значения val:
Выделить структуру struct hash_entry.
Установить entry->value = val.
Добавить в хэш-таблицу: hash_add(ctx->htable, &entry->node, val);

Примечание: hash_add() автоматически вычисляет хэш и добавляет элемент в соответствующую корзину.

4. Поиск числа

Поиск числа x обязательно выполняется через бинарный поиск bsearch():

Проверить валидность x (например, если x >= array_size, можно сразу вернуть "не найдено").
Вычислить индекс корзины: bucket = hash_min(x, HASH_BITS_COUNT);
Собрать все hash_entry из корзины bucket в массив указателей:
Пройти по цепочке hlist в данной корзине.
Посчитать количество элементов.
Выделить массив указателей struct hash_entry ** нужного размера.
Заполнить массив указателями на элементы.
Отсортировать массив указателей по полю value с помощью sort() (функция сравнения сравнивает entry->value).
Выполнить бинарный поиск bsearch() по отсортированному массиву указателей.
Освободить временный массив указателей.

Результат поиска должен сохраняться как «последний результат» в контексте (для последующего чтения через result).

Обязательное требование: поиск внутри корзины должен использовать bsearch() по массиву указателей на hash_entry, отсортированному по полю value.

Интерфейс модульных параметров (module_param_cb)

Модуль должен поддерживать взаимодействие через module_param_cb.

Требуемые параметры:

Параметр	Доступ	Описание	Формат
search	write	Запустить поиск числа в хэш-таблице	echo 123 > /sys/module/kernel_hashtable_search/parameters/search
result	read	Получить результат последнего поиска	cat /sys/module/kernel_hashtable_search/parameters/result
rebuild	write	Пересоздать хэш-таблицу (перегенерация случайных данных)	echo 1 > /sys/module/kernel_hashtable_search/parameters/rebuild
bucket_id	write	Установить индекс корзины для просмотра содержимого	echo 5 > /sys/module/kernel_hashtable_search/parameters/bucket_id
bucket_dump	read	Вывести содержимое корзины с индексом bucket_id	cat /sys/module/kernel_hashtable_search/parameters/bucket_dump

5.1 search

При записи числа x в параметр search модуль должен:
разобрать значение (через kstrtouint());
выполнить поиск x по алгоритму из раздела 4 (с обязательным использованием bsearch());
сохранить результат (нашли/не нашли, значение, индекс корзины) во внутреннем состоянии;
опционально выводить диагностическое сообщение в dmesg.

5.2 result

При чтении параметра result модуль должен вернуть строку в формате, например:

found=1 value=42 bucket=10.

Конкретный формат можно выбрать, но он должен быть:

однострочным,
человекочитаемым,
неизменным между запусками (для удобства проверки).

5.3 rebuild

При записи любого ненулевого значения в rebuild модуль должен:

Удалить все элементы из хэш-таблицы и освободить память:
struct hash_entry *entry;
struct hlist_node *tmp;
int bkt;
hash_for_each_safe(ctx->htable, bkt, tmp, entry, node) {
hash_del(&entry->node);
kfree(entry);
}
Заново сгенерировать элементы и добавить их в хэш-таблицу по алгоритму из раздела 3.

Нулевое значение можно игнорировать (не перестраивать данные).

5.4 bucket_id и bucket_dump

Параметр bucket_id задаёт целочисленный индекс корзины (от 0 до 2^HASH_BITS_COUNT - 1), содержимое которой будет выводиться через bucket_dump.
При чтении bucket_dump модуль должен:
Пройти по всем элементам в корзине bucket_id с помощью hlist_for_each_entry().
Собрать указатели на hash_entry в массив.
Отсортировать массив указателей по полю value с помощью sort().
Вывести значения value в формате, например:
bucket=5 len=7: 1 5 9 13 17 21 25

Требования:

При некорректном bucket_id (>= 2^HASH_BITS_COUNT) возвращать сообщение об ошибке.
Содержимое корзины должно выводиться в отсортированном порядке по полю value.

Требования к безопасности и корректности

Корректно обрабатывать ошибки выделения памяти (kmalloc, kzalloc) и освобождать уже выделенные ресурсы при ошибках и при выгрузке модуля.
Валидировать входные данные в callback-функциях параметров:
обрабатывать неверный формат числа;
проверять диапазоны (x < array_size, bucket_id < 2^num_buckets_bits).
Освобождать всю выделенную память при выгрузке модуля.

Рекомендуемые коды возврата

Определить и документировать коды возврата (пример):

#define BS_OK            0       /* операция успешна */
#define BS_INVALID      -EINVAL  /* неверный параметр */
#define BS_NOMEM        -ENOMEM  /* недостаточно памяти */
#define BS_NOT_FOUND    -ENOENT  /* число не найдено */

Структура проекта

Рекомендуемая структура директории проекта:

kernel_hashtable_search_module/
├── Makefile
├── Kbuild
└── src/
├── main.c        # init/exit, создание/уничтожение контекста
├── params.c      # реализация module_param_cb интерфейса
├── build.c       # генерация данных и построение хэш-таблицы
└── search.c      # функции поиска (bsearch, comparator, сбор элементов корзины)

Модуль должен иметь название kernel_hashtable_search.ko при сборке.

Пример использования после загрузки модуля

# Загрузка модуля с параметрами

sudo insmod kernel_hashtable_search.ko array_size=1000

# Поиск числа 42

echo 42 > /sys/module/kernel_hashtable_search/parameters/search

# Читаем результат

cat /sys/module/kernel_hashtable_search/parameters/result

# Пример вывода:

# found=1 value=42 bucket=10

# Просмотр содержимого корзины 10

echo 10 > /sys/module/kernel_hashtable_search/parameters/bucket_id
cat /sys/module/kernel_hashtable_search/parameters/bucket_dump

# Пример вывода:

# bucket=10 len=15: 10 42 74 106 138 170 202 234 266 298 330 362 394 426 458

# Перегенерация хэш-таблицы

echo 1 > /sys/module/kernel_hashtable_search/parameters/rebuild

# Выгрузка модуля

sudo rmmod kernel_hashtable_search

Формат сдачи

Сдавайте архив со следующей структурой:

студент_фамилия_kernel_hashtable_search.tar.gz
├── Makefile
├── Kbuild
└── src/
├── main.c
├── params.c
├── build.c
└── search.c

Дополнительные указания

Работа с хэш-таблицей ядра

Основные макросы и функции:

#include <linux/hashtable.h>

// Объявление хэш-таблицы (в структуре или глобально)
#define HASH_BITS_COUNT 6
DECLARE_HASHTABLE(name, HASH_BITS_COUNT) // Размер хэш-таблицы должен быть фиксированным и задаваться константой времени компиляции.

// Инициализация
hash_init(hashtable);

// Добавление элемента
hash_add(hashtable, &entry->node, key);

// Поиск по ключу (перебор элементов в корзине)
hash_for_each_possible(hashtable, obj, member, key) { ... }

// Удаление элемента
hash_del(&entry->node);

// Обход всей таблицы
hash_for_each(hashtable, bkt, obj, member) { ... }

// Безопасный обход (можно удалять)
hash_for_each_safe(hashtable, bkt, tmp, obj, member) { ... }

// Вычисление хэша для конкретной корзины
bucket = hash_min(value, bits);


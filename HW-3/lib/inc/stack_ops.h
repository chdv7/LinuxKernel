#ifndef KERNEL_STACK_STACK_OPS_H_
#define KERNEL_STACK_STACK_OPS_H_

#define STACK_OK          0      /* Операция успешна */
#define STACK_EMPTY      -1      /* Стек пуст */
#define STACK_NOMEM      -2      /* Нет памяти */
#define STACK_INVALID    -3      /* Неверный параметр */

void stack_init(void);
int stack_push(int value);
/*
 * stack_pop() и stack_peek() соответствуют сигнатурам задания: они возвращают
 * значение элемента либо отрицательный код ошибки. Если в стеке разрешены
 * отрицательные данные, для однозначной обработки следует использовать
 * stack_pop_value() и stack_peek_value().
 */
int stack_pop(void);
int stack_peek(void);
int stack_is_empty(void);
int stack_size(void);
void stack_clear(void);

/*
 * Вспомогательные функции без неоднозначности между отрицательными данными
 * и отрицательными кодами ошибок. Используются интерфейсом sysfs.
 */
int stack_pop_value(int *value);
int stack_peek_value(int *value);

#endif  /* KERNEL_STACK_STACK_OPS_H_ */

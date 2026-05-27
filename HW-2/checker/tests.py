from executor import Executor

class Tests:
    def __init__(self, ops):
        self.exec = Executor(ops)

    def idx_tests(self):
        test_cases = [
            {"write": "0", "expected": "0"},
            {"write": "2", "expected": "2"},
            {"write": "A", "expected": "[Errno 22] Invalid argument"},
            #{"write": "2", "expected": "2"},
        ]
        print("\n--- Запуск тестов idx ---")
        results = []
        for case in test_cases:
            results.append(self.exec.idx_exec_test(case))
        print("\nТесты idx завершены:", "OK" if all(results) else "FAIL")
        return all(results)

    def ch_val_tests(self):
        test_cases = [
            {"write": "A", "expected": "A"},
            {"write": "123", "expected": "1"},
            {"write": "\n", "expected": "[Errno 22] Invalid argument"},
        ]
        print("\n--- Запуск тестов ch_val ---")
        results = []
        for case in test_cases:
            results.append(self.exec.ch_val_exec_test(case))
        print("\nТесты ch_val завершены:", "OK" if all(results) else "FAIL")
        return all(results)

    def str_buf_tests(self):
        test_cases = [
            {"write": "Hello", "expected": "Hello"},
            {"write": "World!", "expected": "World!"},
            {"write": "Hello, World!", "expected": "Hello, World!"},
        ]
        print("\n--- Запуск тестов str_buf ---")
        results = []
        for case in test_cases:
            results.append(self.exec.str_buf_exec_test(case))
        print("\nТесты str_buf завершены:", "OK" if all(results) else "FAIL")
        return all(results)

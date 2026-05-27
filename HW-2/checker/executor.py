from module_ops import ModuleOps

class Executor:
    def __init__(self, ops):
        self.ops = ops

    def idx_exec_test(self, case):
        self.ops.write_idx(case['write'])
        value = self.ops.read_idx()
        print(f"idx_test: записано {case['write']}, прочитано {value}, ожидалось {case['expected']}")
        return str(value) == case['expected']

    def ch_val_exec_test(self, case):
        self.ops.write_ch_val(case['write'])
        value = self.ops.read_ch_val()
        print(f"ch_val_test: записано {case['write']}, прочитано {value}, ожидалось {case['expected']}")
        return str(value) == case['expected']

    def str_buf_exec_test(self, case):
        _str = case['write']
        for idx in range (0, 13):
            self.ops.write_idx(str(idx))
            self.ops.write_ch_val(" ")
        for idx in range (0, len(_str)):
            self.ops.write_idx(str(idx))
            self.ops.write_ch_val(_str[idx])
        str_buf = self.ops.read_str_buf()
        print(f"str_buf_test: записано {case['write']}, прочитано {str_buf}, ожидалось {case['expected']}")
        return str_buf.strip() == case['expected']

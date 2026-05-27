import sys

class ModuleOps:
    def __init__(self, idx_file, ch_val_file, str_buf_file):
        self.idx_file = idx_file
        self.ch_val_file = ch_val_file
        self.str_buf_file = str_buf_file

    def write_idx(self, value:str):
        try:
            self.idx_file.seek(0)
            self.idx_file.write(value)
            self.idx_file.flush()
        except Exception as e:
            return str(e)

    def write_ch_val(self, value:str):
        try:
            val_str = str(value)
            self.ch_val_file.seek(0)
            self.ch_val_file.write(val_str[0])
            self.ch_val_file.flush()
        except Exception as e:
            return str(e)
            
    def read_idx(self):
        try:
            self.idx_file.seek(0)
            return int(self.idx_file.read().strip())
        except Exception as e:
            return str(e)

    def read_ch_val(self):
        try:
            self.ch_val_file.seek(0)
            ch = self.ch_val_file.read(1).strip()
            return ch
        except Exception as e:
            return str(e)

    def read_str_buf(self):
        self.str_buf_file.seek(0)
        str_buf = self.str_buf_file.read()
        return str_buf

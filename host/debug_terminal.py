"""Nonblocking local terminal input; no worker thread touches the WPD COM object."""
import os
import shutil
import sys


class DebugTerminal:
    def __init__(self):
        self.saved = []
        self.surrogate = ''

    @staticmethod
    def check():
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise ValueError('shell requires an interactive terminal; use exec --stdin for redirected input')

    def __enter__(self):
        self.check()
        try:
            if os.name == 'nt':
                import ctypes
                from ctypes import wintypes
                self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
                self.kernel.GetStdHandle.argtypes = [wintypes.DWORD]
                self.kernel.GetStdHandle.restype = wintypes.HANDLE
                self.kernel.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
                self.kernel.GetConsoleMode.restype = wintypes.BOOL
                self.kernel.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
                self.kernel.SetConsoleMode.restype = wintypes.BOOL
                for which in (-10, -11):
                    handle = self.kernel.GetStdHandle(which & 0xffffffff)
                    mode = wintypes.DWORD()
                    if not self.kernel.GetConsoleMode(handle, ctypes.byref(mode)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    self.saved.append((handle, mode.value))
                    # ReadConsoleW/_getwch input, processed/line/echo disabled.
                    # Disable Quick Edit so mouse selection cannot freeze input.
                    value = ((mode.value | 0x80) & ~(0x1 | 0x2 | 0x4 | 0x40 | 0x200)
                             if which == -10 else mode.value | 0x4)
                    if not self.kernel.SetConsoleMode(handle, value):
                        raise ctypes.WinError(ctypes.get_last_error())
            else:
                import termios
                import tty
                self.saved = [(sys.stdin.fileno(), termios.tcgetattr(sys.stdin.fileno()))]
                tty.setraw(sys.stdin.fileno())
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        for handle, mode in reversed(self.saved):
            if os.name == 'nt':
                self.kernel.SetConsoleMode(handle, mode)
            else:
                import termios
                termios.tcsetattr(handle, termios.TCSADRAIN, mode)
        self.saved = []

    def size(self):
        size = shutil.get_terminal_size((100, 30))
        return min(4096, max(1, size.lines)), min(4096, max(1, size.columns))

    def poll(self):
        if os.name != 'nt':
            import select
            if not select.select([sys.stdin], [], [], 0)[0]:
                return b''
            return os.read(sys.stdin.fileno(), 256) or None
        import msvcrt
        keys = {'H': '\x1b[A', 'P': '\x1b[B', 'M': '\x1b[C', 'K': '\x1b[D',
                'G': '\x1b[H', 'O': '\x1b[F', 'S': '\x1b[3~', 'R': '\x1b[2~',
                'I': '\x1b[5~', 'Q': '\x1b[6~'}
        text, self.surrogate = self.surrogate, ''
        for _ in range(128):
            if not msvcrt.kbhit():
                break
            char = msvcrt.getwch()
            if char in ('\x00', '\xe0'):
                char = keys.get(msvcrt.getwch(), '')
            elif char == '\x08':
                char = '\x7f'  # Linux PTY default VERASE.
            text += char
        if text and 0xd800 <= ord(text[-1]) <= 0xdbff:
            self.surrogate, text = text[-1], text[:-1]
        # Windows returns UTF-16 code units; a pair can straddle polls.
        return text.encode('utf-16-le', 'surrogatepass').decode('utf-16-le', 'replace').encode('utf-8')

    @staticmethod
    def emit(stream, data):
        target = sys.stdout if stream == 0 else sys.stderr
        target.buffer.write(data)
        target.buffer.flush()

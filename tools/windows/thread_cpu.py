"""Opt-in Windows thread CPU accounting without suspending the game.

This measures accumulated execution time, not call stacks or time spent waiting.
Snapshot/query overhead makes these diagnostic runs separate from FPS baselines.
"""
import ctypes
from ctypes import wintypes
import csv
import time


class ThreadEntry(ctypes.Structure):
    _fields_ = [('size', wintypes.DWORD), ('usage', wintypes.DWORD),
                ('thread_id', wintypes.DWORD), ('owner_id', wintypes.DWORD),
                ('base_priority', wintypes.LONG), ('delta_priority', wintypes.LONG),
                ('flags', wintypes.DWORD)]


class ThreadSampler:
    def __init__(self, pid, path):
        self.pid, self.handles, self.previous = pid, {}, {}
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'CreateToolhelp32Snapshot': ([wintypes.DWORD, wintypes.DWORD], wintypes.HANDLE),
            'Thread32First': ([wintypes.HANDLE, ctypes.POINTER(ThreadEntry)], wintypes.BOOL),
            'Thread32Next': ([wintypes.HANDLE, ctypes.POINTER(ThreadEntry)], wintypes.BOOL),
            'OpenThread': ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            'GetThreadTimes': ([wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4, wintypes.BOOL),
            'GetThreadDescription': ([wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)], wintypes.LONG),
            'CloseHandle': ([wintypes.HANDLE], wintypes.BOOL),
            'LocalFree': ([ctypes.c_void_p], ctypes.c_void_p),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = args, result
        self.stream = path.open('w', newline='', encoding='utf-8')
        self.writer = csv.writer(self.stream)
        self.writer.writerow(['clock_ns', 'previous_ns', 'thread_id', 'name', 'kernel_ns', 'user_ns'])
        self.sample()

    @staticmethod
    def nanoseconds(value):
        return ((value.dwHighDateTime << 32) | value.dwLowDateTime) * 100

    def discover(self):
        snapshot = self.api.CreateToolhelp32Snapshot(4, 0) # TH32CS_SNAPTHREAD
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            entry = ThreadEntry()
            entry.size = ctypes.sizeof(entry)
            more = self.api.Thread32First(snapshot, ctypes.byref(entry))
            while more:
                tid = entry.thread_id
                if entry.owner_id == self.pid and tid not in self.handles:
                    handle = self.api.OpenThread(0x0800, False, tid) # QUERY_LIMITED_INFORMATION
                    if handle:
                        self.handles[tid] = handle, self.description(handle)
                more = self.api.Thread32Next(snapshot, ctypes.byref(entry))
        finally:
            self.api.CloseHandle(snapshot)

    def description(self, handle):
        text = ctypes.c_void_p()
        if self.api.GetThreadDescription(handle, ctypes.byref(text)) < 0 or not text.value:
            return ''
        try:
            return ctypes.wstring_at(text)
        finally:
            self.api.LocalFree(text)

    def sample(self):
        self.discover()
        for tid, (handle, name) in list(self.handles.items()):
            # Threads may be discovered before their entry point sets the name.
            if not name:
                name = self.description(handle)
                self.handles[tid] = handle, name
            creation, exit_time, kernel, user = (wintypes.FILETIME() for _ in range(4))
            if self.api.GetThreadTimes(handle, ctypes.byref(creation), ctypes.byref(exit_time),
                                       ctypes.byref(kernel), ctypes.byref(user)):
                now = time.perf_counter_ns()
                k, u = self.nanoseconds(kernel), self.nanoseconds(user)
                previous = self.previous.get(tid)
                if previous:
                    clock, old_k, old_u = previous
                    self.writer.writerow([now, clock, tid, name, max(0, k - old_k), max(0, u - old_u)])
                self.previous[tid] = now, k, u
                if self.nanoseconds(exit_time) == 0:
                    continue
            self.api.CloseHandle(handle)
            del self.handles[tid]
            self.previous.pop(tid, None)
        self.stream.flush()

    def close(self):
        for handle, _ in self.handles.values():
            self.api.CloseHandle(handle)
        self.handles.clear()
        self.stream.close()


def summarize_threads(path, start, end):
    totals, names = {}, {}
    with path.open(encoding='utf-8', newline='') as stream:
        for row in csv.DictReader(stream):
            if int(row['previous_ns']) < start or int(row['clock_ns']) > end:
                continue
            tid = int(row['thread_id'])
            if row['name']:
                names[tid] = row['name']
            totals[tid] = totals.get(tid, 0) + int(row['kernel_ns']) + int(row['user_ns'])
    return [dict(thread_id=tid, name=names.get(tid, ''), cpu_ms=cpu / 1e6,
                 percent_of_one_core=100 * cpu / (end - start))
            for tid, cpu in sorted(totals.items(), key=lambda item: item[1], reverse=True)[:12]]

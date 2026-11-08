import argparse
import ctypes
import functools
import os
import sys
import tempfile

OK = 0
NOT_FOUND = 2
IO_ERROR = 5
INVALID_FORMAT = 6


class LuxError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


@functools.cache
def load(path=None):
    path = path or os.environ.get("LUXCORE_LIB")
    if not path:
        raise LuxError(1, "pass the library path or set LUXCORE_LIB")
    lib = ctypes.CDLL(os.path.abspath(path))

    handle = ctypes.c_void_p
    out = ctypes.POINTER(ctypes.c_void_p)
    size = ctypes.c_size_t
    text = ctypes.c_char_p

    signatures = {
        "lux_status_message": (text, [ctypes.c_int]),
        "lux_index_create": (ctypes.c_int, [out]),
        "lux_index_free": (None, [handle]),
        "lux_index_add": (ctypes.c_int, [handle, text, size, text, size, text, size]),
        "lux_index_remove": (ctypes.c_int, [handle, text, size, ctypes.POINTER(ctypes.c_int)]),
        "lux_index_size": (ctypes.c_int, [handle, ctypes.POINTER(size)]),
        "lux_index_save": (ctypes.c_int, [handle, text, size]),
        "lux_index_load": (ctypes.c_int, [text, size, out]),
        "lux_index_stored": (ctypes.c_int, [handle, text, size, text, size, out]),
        "lux_index_search": (ctypes.c_int, [handle, text, size, size, out]),
        "lux_index_suggest": (ctypes.c_int, [handle, text, size, size, out]),
        "lux_hits_count": (size, [handle]),
        "lux_hits_key": (ctypes.c_void_p, [handle, size, ctypes.POINTER(size)]),
        "lux_hits_score": (ctypes.c_double, [handle, size]),
        "lux_hits_free": (None, [handle]),
        "lux_strings_count": (size, [handle]),
        "lux_strings_get": (ctypes.c_void_p, [handle, size, ctypes.POINTER(size)]),
        "lux_strings_free": (None, [handle]),
        "lux_text_data": (ctypes.c_void_p, [handle]),
        "lux_text_length": (size, [handle]),
        "lux_text_free": (None, [handle]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(lib, name)
        function.restype = restype
        function.argtypes = argtypes
    return lib


def utf8(value):
    data = value.encode("utf-8")
    return data, len(data)


class Index:
    def __init__(self, library=None, *, _handle=None):
        self._handle = None
        self._lib = load(library)
        if _handle is None:
            _handle = ctypes.c_void_p()
            self._check(self._lib.lux_index_create(ctypes.byref(_handle)))
        self._handle = _handle

    @classmethod
    def open(cls, path, library=None):
        lib = load(library)
        handle = ctypes.c_void_p()
        status = lib.lux_index_load(*utf8(os.fspath(path)), ctypes.byref(handle))
        if status != OK:
            raise LuxError(status, lib.lux_status_message(status).decode("ascii"))
        return cls(library, _handle=handle)

    def save(self, path):
        self._check(self._lib.lux_index_save(self._handle, *utf8(os.fspath(path))))

    def close(self):
        if self._handle:
            self._lib.lux_index_free(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()

    def add(self, key, title, body):
        self._check(self._lib.lux_index_add(self._handle, *utf8(key), *utf8(title), *utf8(body)))

    def remove(self, key):
        removed = ctypes.c_int()
        self._check(self._lib.lux_index_remove(self._handle, *utf8(key), ctypes.byref(removed)))
        return bool(removed.value)

    def __len__(self):
        size = ctypes.c_size_t()
        self._check(self._lib.lux_index_size(self._handle, ctypes.byref(size)))
        return size.value

    def stored(self, key, field):
        result = ctypes.c_void_p()
        status = self._lib.lux_index_stored(self._handle, *utf8(key), *utf8(field), ctypes.byref(result))
        if status == NOT_FOUND:
            return None
        self._check(status)
        try:
            data = self._lib.lux_text_data(result)
            return ctypes.string_at(data, self._lib.lux_text_length(result)).decode("utf-8")
        finally:
            self._lib.lux_text_free(result)

    def search(self, query, limit=10):
        result = ctypes.c_void_p()
        self._check(self._lib.lux_index_search(self._handle, *utf8(query), limit, ctypes.byref(result)))
        try:
            hits = []
            length = ctypes.c_size_t()
            for position in range(self._lib.lux_hits_count(result)):
                key = self._lib.lux_hits_key(result, position, ctypes.byref(length))
                hits.append((ctypes.string_at(key, length.value).decode("utf-8"),
                             self._lib.lux_hits_score(result, position)))
            return hits
        finally:
            self._lib.lux_hits_free(result)

    def suggest(self, query, limit=5):
        result = ctypes.c_void_p()
        self._check(self._lib.lux_index_suggest(self._handle, *utf8(query), limit, ctypes.byref(result)))
        try:
            terms = []
            length = ctypes.c_size_t()
            for position in range(self._lib.lux_strings_count(result)):
                term = self._lib.lux_strings_get(result, position, ctypes.byref(length))
                terms.append(ctypes.string_at(term, length.value).decode("utf-8"))
            return terms
        finally:
            self._lib.lux_strings_free(result)

    def _check(self, status):
        if status != OK:
            raise LuxError(status, self._lib.lux_status_message(status).decode("ascii"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("query", nargs="?", default="shoping lis")
    parser.add_argument("--lib")
    args = parser.parse_args()

    with Index(args.lib) as index:
        index.add("note-1", "Shopping list", "milk, eggs, bread and butter")
        index.add("note-2", "Reading list", "a book about how Lucene ranks search results")
        index.add("note-3", "Trip planning", "book the train to Edinburgh and pack a raincoat")

        print(f'Searching {len(index)} notes for "{args.query}"')
        hits = index.search(args.query)
        if not hits:
            print("  no results")
        for key, score in hits:
            print(f"  {key}  {index.stored(key, 'title')}  {score:.3f}")

        print("Suggestions for \"rea\":", index.suggest("rea"))

        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "notes.lux")
            index.save(path)
            with Index.open(path, args.lib) as reloaded:
                reloaded_hits = reloaded.search(args.query)
                if len(reloaded) != len(index) or reloaded_hits != hits:
                    sys.exit("the reloaded index differs from the saved one")
                print(f"Reloaded {len(reloaded)} notes from a file")
                for key, score in reloaded_hits[:1]:
                    print(f"  {key}  {reloaded.stored(key, 'title')}  {score:.3f}")
            try:
                Index.open(os.path.join(directory, "missing.lux"), args.lib)
            except LuxError as error:
                print(f"Missing file rejected with status {error.status}: {error}")

        try:
            index.add("", "title", "body")
        except LuxError as error:
            print(f"Empty key rejected with status {error.status}: {error}")

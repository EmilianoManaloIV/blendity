import io, os, sys

W = os.environ.get("BL_W", r"W:\blendity")


def read(path):
    s = io.open(path, encoding="utf-8", newline="").read()
    return s.replace("\r\n", "\n"), "\r\n" in s


def write(path, s, crlf):
    io.open(path, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if crlf else s)


def patch(path, pairs):
    """Each old text must occur exactly once; the file's newlines kept."""
    s, crlf = read(path)
    for old, new in pairs:
        n = s.count(old)
        if n != 1:
            sys.exit("%d times in %s: %r" % (n, path, old[:100]))
        s = s.replace(old, new)
    write(path, s, crlf)
    print("patched", path)


def new_file(path, text, crlf=True):
    write(path, text.replace("\r\n", "\n"), crlf)
    print("wrote", path)

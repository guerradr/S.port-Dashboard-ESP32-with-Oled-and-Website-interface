Import("env")
import gzip
import os

# Gzip the web UI before building/uploading the LittleFS image.
# The firmware serves index.html.gz with the gzip content header when present,
# and falls back to the plain index.html otherwise.
# The compression runs when PlatformIO loads this env (pre: script) and again
# right before the filesystem image is assembled.


def gzip_web(*_args, **_kwargs):
    project_dir = env.subst("$PROJECT_DIR")
    src = os.path.join(project_dir, "data", "index.html")
    dst = src + ".gz"
    if not os.path.exists(src):
        return
    with open(src, "rb") as fh:
        raw = fh.read()
    with open(dst, "wb") as fh:
        fh.write(gzip.compress(raw, 9))
    print("gzip_web: %d -> %d bytes" % (len(raw), os.path.getsize(dst)))


gzip_web()
env.AddPreAction("$BUILD_DIR/littlefs.bin", gzip_web)

import json, sys, glob, os

for path in sys.argv[1:]:
    print("=== crash report:", path)
    try:
        raw = open(path, errors="replace").read()
        head, _, body = raw.partition("\n")
        try:
            data = json.loads(body)
        except ValueError:
            print(raw[:4000])
            continue
        print("exception:", data.get("exception"))
        print("termination:", data.get("termination"))
        print("asi:", data.get("asi"))
        images = data.get("usedImages", [])
        threads = data.get("threads", [])
        print("threads:", len(threads))
        for i, t in enumerate(threads):
            if t.get("triggered") or i == data.get("faultingThread"):
                print("--- faulting thread", i, t.get("name") or t.get("queue"))
                for f in t.get("frames", [])[:45]:
                    img = images[f["imageIndex"]] if f.get("imageIndex") is not None else {}
                    print("  %-28s %-40s +%s" % (
                        os.path.basename(img.get("path", img.get("name", "?")))[:28],
                        f.get("symbol", "?")[:40], f.get("symbolLocation", f.get("imageOffset"))))
    except Exception as exc:
        print("failed to parse:", exc)

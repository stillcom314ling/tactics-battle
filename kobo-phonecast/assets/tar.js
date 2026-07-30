/* ustar writer for KoboRoot.tgz.
 *
 * This is the load-bearing code: the archive it produces is extracted as root
 * over / by Nickel on a device with no recovery mode. Two rules follow from
 * that, and both are enforced here rather than trusted to callers:
 *
 *   - every path is checked before it is written, not after;
 *   - the header checksum is computed the one way tar actually reads it.
 *
 * No DOM, no imports. Loads as a plain <script> and as a CommonJS module so the
 * verification harness exercises the same bytes the browser produces.
 */
var KoboTar = (function () {
    "use strict";

    var BLOCK = 512;

    /* The only tree this writer is allowed to touch. */
    var PLUGIN_DIR = "mnt/onboard/.adds/koreader/plugins/phonecast.koplugin/";

    /* Extracting over / makes these fatal, not merely wrong. */
    var FORBIDDEN = [
        "usr/", "etc/", "bin/", "lib/", "sbin/", "opt/", "var/", "dev/", "proc/", "sys/",
        "mnt/onboard/.kobo/"
    ];

    /* Fixed so two bundles built from the same settings are byte-identical, and
     * so the only difference between bundles built from different settings is
     * config.lua. 2025-01-01T00:00:00Z. */
    var MTIME = 1735689600;

    var enc = typeof TextEncoder !== "undefined" ? new TextEncoder() : null;

    function utf8(str) {
        if (enc) return enc.encode(str);
        return new Uint8Array(Buffer.from(str, "utf8"));
    }

    /* --------------------------------------------------------------- paths */

    function assertSafe(path) {
        if (typeof path !== "string" || path.length === 0) {
            throw new Error("tar: empty path");
        }
        if (path.charAt(0) === "/") {
            throw new Error("tar: absolute path: " + path);
        }
        if (path.indexOf("..") !== -1) {
            throw new Error("tar: path contains '..': " + path);
        }
        if (path.indexOf("\\") !== -1 || path.indexOf("//") !== -1) {
            throw new Error("tar: malformed path: " + path);
        }
        for (var i = 0; i < FORBIDDEN.length; i++) {
            if (path.indexOf(FORBIDDEN[i]) === 0) {
                throw new Error("tar: path writes to " + FORBIDDEN[i] + ": " + path);
            }
        }
    }

    function assertInPluginDir(path) {
        if (path.indexOf(PLUGIN_DIR) !== 0) {
            throw new Error("tar: path escapes " + PLUGIN_DIR + ": " + path);
        }
    }

    /* ustar splits long names across prefix[155] and name[100], joined by "/".
     * Nothing this generator writes comes close, but a silent truncation here
     * would produce an archive that extracts to the wrong place. */
    function splitName(path) {
        var bytes = utf8(path);
        if (bytes.length <= 100) return { prefix: "", name: path };

        var cut = path.length - 1;
        while (cut > 0) {
            var slash = path.lastIndexOf("/", cut);
            if (slash <= 0) break;
            var name = path.slice(slash + 1);
            var prefix = path.slice(0, slash);
            if (utf8(name).length <= 100 && utf8(prefix).length <= 155) {
                return { prefix: prefix, name: name };
            }
            cut = slash - 1;
        }
        throw new Error("tar: path too long for ustar: " + path);
    }

    /* -------------------------------------------------------------- headers */

    function writeStr(buf, offset, field, str) {
        var bytes = utf8(str);
        if (bytes.length > field) throw new Error("tar: field overflow: " + str);
        buf.set(bytes, offset);
    }

    /* Octal, zero-padded, NUL-terminated: field-1 digits then \0. */
    function writeOctal(buf, offset, field, value) {
        var digits = field - 1;
        var str = Math.floor(value).toString(8);
        if (str.length > digits) {
            throw new Error("tar: value does not fit in " + field + " octal bytes: " + value);
        }
        while (str.length < digits) str = "0" + str;
        writeStr(buf, offset, digits, str);
        buf[offset + digits] = 0;
    }

    function header(path, size, isDir) {
        var buf = new Uint8Array(BLOCK);
        var parts = splitName(path);

        writeStr(buf, 0, 100, parts.name);
        writeOctal(buf, 100, 8, isDir ? 0o755 : 0o644);
        writeOctal(buf, 108, 8, 0);                  // uid
        writeOctal(buf, 116, 8, 0);                  // gid
        writeOctal(buf, 124, 12, isDir ? 0 : size);
        writeOctal(buf, 136, 12, MTIME);

        /* Checksum is computed with these eight bytes held as spaces. Set them
         * now, sum, then overwrite — getting this backwards yields an archive
         * some desktop tools open happily and the device rejects in silence. */
        for (var i = 148; i < 156; i++) buf[i] = 0x20;

        buf[156] = isDir ? 0x35 : 0x30;              // typeflag '5' | '0'
        writeStr(buf, 257, 6, "ustar");              // magic, NUL-terminated
        buf[262] = 0;
        buf[263] = 0x30; buf[264] = 0x30;            // version "00"
        writeStr(buf, 265, 32, "root");              // uname
        writeStr(buf, 297, 32, "root");              // gname
        writeOctal(buf, 329, 8, 0);                  // devmajor
        writeOctal(buf, 337, 8, 0);                  // devminor
        if (parts.prefix) writeStr(buf, 345, 155, parts.prefix);

        var sum = 0;
        for (var j = 0; j < BLOCK; j++) sum += buf[j];

        /* Six octal digits, NUL, then space — the layout every extractor
         * accepts, including the one on the device. */
        var oct = sum.toString(8);
        while (oct.length < 6) oct = "0" + oct;
        writeStr(buf, 148, 6, oct);
        buf[154] = 0;
        buf[155] = 0x20;

        return buf;
    }

    /* ---------------------------------------------------------------- build */

    /* entries: [{ path, data }] for files, [{ path, dir: true }] for dirs.
     * Directory paths must end in "/". Order is preserved: emit a directory
     * before anything inside it. */
    function buildTar(entries) {
        var blocks = [];
        var seen = {};

        entries.forEach(function (e) {
            assertSafe(e.path);
            if (seen[e.path]) throw new Error("tar: duplicate path: " + e.path);
            seen[e.path] = true;

            if (e.dir) {
                if (e.path.charAt(e.path.length - 1) !== "/") {
                    throw new Error("tar: directory path must end in '/': " + e.path);
                }
                blocks.push(header(e.path, 0, true));
                return;
            }

            var data = typeof e.data === "string" ? utf8(e.data) : e.data;
            blocks.push(header(e.path, data.length, false));
            var padded = Math.ceil(data.length / BLOCK) * BLOCK;
            var body = new Uint8Array(padded);
            body.set(data, 0);
            blocks.push(body);
        });

        blocks.push(new Uint8Array(BLOCK * 2));      // end-of-archive

        var total = blocks.reduce(function (n, b) { return n + b.length; }, 0);
        var out = new Uint8Array(total);
        var at = 0;
        blocks.forEach(function (b) { out.set(b, at); at += b.length; });
        return out;
    }

    /* --------------------------------------------------------- plugin bundle */

    function luaString(str) {
        return '"' + String(str).replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
    }

    function clampInt(value, lo, hi, fallback) {
        var n = Math.floor(Number(value));
        if (!isFinite(n)) return fallback;
        return Math.min(hi, Math.max(lo, n));
    }

    function normaliseConfig(cfg) {
        cfg = cfg || {};
        var dir = typeof cfg.save_dir === "string" ? cfg.save_dir.trim() : "";
        return {
            port: clampInt(cfg.port, 1, 65535, 8080),
            poll: clampInt(cfg.poll, 1, 60, 1),
            keep: clampInt(cfg.keep, 1, 9999, 30),
            max_body: clampInt(cfg.max_body, 1024, 512 * 1024 * 1024, 32 * 1024 * 1024),
            save_dir: dir
        };
    }

    function renderConfig(cfg) {
        var c = normaliseConfig(cfg);
        var lines = [
            "-- Phone Cast settings, written by the install page.",
            "-- Edit and restart KOReader to change them. Delete this file to",
            "-- fall back to the plugin's own defaults.",
            "return {",
            "    port = " + c.port + ",",
            "    poll = " + c.poll + ",",
            "    keep = " + c.keep + ",",
            "    max_body = " + c.max_body + ","
        ];
        if (c.save_dir) lines.push("    save_dir = " + luaString(c.save_dir) + ",");
        lines.push("}", "");
        return lines.join("\n");
    }

    /* files: { "_meta.lua": "...", "main.lua": "..." } shipped verbatim. */
    function buildPluginTar(files, cfg) {
        var entries = [{ path: PLUGIN_DIR, dir: true }];

        Object.keys(files).sort().forEach(function (name) {
            entries.push({ path: PLUGIN_DIR + name, data: files[name] });
        });
        entries.push({ path: PLUGIN_DIR + "config.lua", data: renderConfig(cfg) });

        entries.forEach(function (e) { assertInPluginDir(e.path); });
        return buildTar(entries);
    }

    /* gzip is supplied by the caller (fflate in the browser, the same vendored
     * copy in the harness) so this file stays dependency-free. */
    function buildPluginBundle(files, cfg, gzip) {
        return gzip(buildPluginTar(files, cfg), { level: 9, mtime: 0 });
    }

    return {
        PLUGIN_DIR: PLUGIN_DIR,
        MTIME: MTIME,
        assertSafe: assertSafe,
        buildTar: buildTar,
        renderConfig: renderConfig,
        normaliseConfig: normaliseConfig,
        buildPluginTar: buildPluginTar,
        buildPluginBundle: buildPluginBundle
    };
})();

if (typeof module !== "undefined" && module.exports) module.exports = KoboTar;

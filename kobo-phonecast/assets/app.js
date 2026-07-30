/* Two jobs: the panel, and the builder.
 *
 * The panel is the page's only moving part. It draws a phone screenshot,
 * Floyd-Steinberg dithers it to one bit, and plays the invert-and-settle flash
 * a real e-ink panel performs on a full refresh -- once, in under 600 ms. It
 * exists to show the visitor what the software does before they read a word. */
(function () {
    "use strict";

    /* ------------------------------------------------------------- the panel */

    /* A phone screenshot of the thing you'd actually want on a bigger screen:
     * a comic page. Drawn rather than photographed -- a real screenshot would
     * be the heaviest asset on the page and would date within a year. */
    function drawScreenshot(g, w, h) {
        function gray(v) { return "rgb(" + v + "," + v + "," + v + ")"; }

        g.fillStyle = gray(255);
        g.fillRect(0, 0, w, h);

        /* Status bar. */
        g.fillStyle = gray(28);
        g.font = "600 9px " + "ui-sans-serif, Helvetica, Arial, sans-serif";
        g.fillText("22:41", 8, 12);
        for (var b = 0; b < 4; b++) {
            g.fillRect(w - 46 + b * 5, 10 - b * 2, 3, 3 + b * 2);
        }
        g.strokeStyle = gray(28);
        g.lineWidth = 1;
        g.strokeRect(w - 22.5, 4.5, 15, 8);
        g.fillRect(w - 21, 6, 9, 5);

        var pad = 8;
        var top = 20;

        /* Panel one: a night sky over rooftops. Continuous tone on purpose --
         * it is what dithering is for. */
        var a = { x: pad, y: top, w: w - pad * 2, h: 150 };
        var sky = g.createLinearGradient(0, a.y, 0, a.y + a.h);
        sky.addColorStop(0, gray(232));
        sky.addColorStop(1, gray(120));
        g.fillStyle = sky;
        g.fillRect(a.x, a.y, a.w, a.h);

        /* Hard-edged, pure white. A soft moon dithers into a smear, because a
         * gentle tonal step is exactly what one bit cannot hold. */
        g.fillStyle = gray(255);
        g.beginPath();
        g.arc(a.x + a.w * 0.72, a.y + 44, 26, 0, Math.PI * 2);
        g.fill();

        /* Rooftops: a run of blocks along the bottom edge, tallest in the middle.
         * [left, width, height] in a 268-wide space, scaled to the panel. */
        var base = a.y + a.h;
        var unit = a.w / 268;
        var blocks = [[0, 34, 46], [30, 32, 30], [58, 40, 74], [94, 46, 40],
                      [136, 42, 84], [174, 50, 54], [218, 50, 64]];
        g.fillStyle = gray(18);
        for (var i = 0; i < blocks.length; i++) {
            g.fillRect(a.x + blocks[i][0] * unit, base - blocks[i][2],
                       blocks[i][1] * unit, blocks[i][2]);
        }

        /* Lit windows, two per block, set into the face. */
        g.fillStyle = gray(240);
        for (var b2 = 0; b2 < blocks.length; b2++) {
            var bl = blocks[b2];
            for (var r = 0; r < 2; r++) {
                g.fillRect(a.x + (bl[0] + 7 + r * 13) * unit, base - bl[2] + 9 + (b2 % 2) * 11, 3, 5);
                g.fillRect(a.x + (bl[0] + 7 + r * 13) * unit, base - bl[2] + 26 + (b2 % 3) * 9, 3, 5);
            }
        }

        g.strokeStyle = gray(0);
        g.lineWidth = 1.5;
        g.strokeRect(a.x + 0.75, a.y + 0.75, a.w - 1.5, a.h - 1.5);

        /* Panel two: a figure, in tone, with dialogue. */
        var c = { x: pad, y: a.y + a.h + 6, w: w - pad * 2, h: h - (a.y + a.h + 6) - pad };
        g.fillStyle = gray(246);
        g.fillRect(c.x, c.y, c.w, c.h);

        var body = g.createLinearGradient(c.x, 0, c.x + c.w, 0);
        body.addColorStop(0, gray(70));
        body.addColorStop(0.55, gray(150));
        body.addColorStop(1, gray(40));
        g.fillStyle = body;
        g.beginPath();
        g.moveTo(c.x + c.w * 0.18, c.y + c.h);
        g.bezierCurveTo(c.x + c.w * 0.2, c.y + c.h * 0.42,
                        c.x + c.w * 0.62, c.y + c.h * 0.42,
                        c.x + c.w * 0.66, c.y + c.h);
        g.closePath();
        g.fill();

        g.fillStyle = gray(196);
        g.beginPath();
        g.arc(c.x + c.w * 0.42, c.y + c.h * 0.40, c.h * 0.17, 0, Math.PI * 2);
        g.fill();
        g.fillStyle = gray(24);
        g.beginPath();
        g.arc(c.x + c.w * 0.42, c.y + c.h * 0.32, c.h * 0.17, Math.PI, 0);
        g.fill();

        /* Speech bubble. */
        var bx = c.x + c.w * 0.50, by = c.y + 10, bw = c.w * 0.44, bh = 46;
        g.fillStyle = gray(255);
        g.strokeStyle = gray(0);
        g.lineWidth = 1.5;
        g.beginPath();
        g.moveTo(bx, by);
        g.lineTo(bx + bw, by);
        g.lineTo(bx + bw, by + bh);
        g.lineTo(bx + 22, by + bh);
        g.lineTo(bx + 10, by + bh + 9);
        g.lineTo(bx + 13, by + bh);
        g.lineTo(bx, by + bh);
        g.closePath();
        g.fill();
        g.stroke();

        g.fillStyle = gray(20);
        var lines = [0.72, 0.86, 0.56, 0.80];
        for (var l = 0; l < lines.length; l++) {
            g.fillRect(bx + 8, by + 9 + l * 9, (bw - 16) * lines[l], 3);
        }

        g.strokeStyle = gray(0);
        g.lineWidth = 1.5;
        g.strokeRect(c.x + 0.75, c.y + 0.75, c.w - 1.5, c.h - 1.5);
    }

    /* Floyd-Steinberg, serpentine-free, straight down the raster. Error is
     * pushed into the four neighbours ahead; the result is one bit per pixel,
     * which is all the panel has. */
    function dither(image) {
        var w = image.width, h = image.height, d = image.data;
        var lum = new Float32Array(w * h);
        for (var i = 0, p = 0; i < d.length; i += 4, p++) {
            lum[p] = 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
        }
        for (var y = 0; y < h; y++) {
            for (var x = 0; x < w; x++) {
                var k = y * w + x;
                var old = lum[k];
                var nw = old < 128 ? 0 : 255;
                var err = old - nw;
                lum[k] = nw;
                if (x + 1 < w) lum[k + 1] += err * 7 / 16;
                if (y + 1 < h) {
                    if (x > 0) lum[k + w - 1] += err * 3 / 16;
                    lum[k + w] += err * 5 / 16;
                    if (x + 1 < w) lum[k + w + 1] += err * 1 / 16;
                }
            }
        }
        var out = new ImageData(w, h);
        for (var q = 0, o = 0; q < lum.length; q++, o += 4) {
            var v = lum[q] | 0;
            out.data[o] = out.data[o + 1] = out.data[o + 2] = v;
            out.data[o + 3] = 255;
        }
        return out;
    }

    function invert(image) {
        var out = new ImageData(image.width, image.height);
        for (var i = 0; i < image.data.length; i += 4) {
            out.data[i] = out.data[i + 1] = out.data[i + 2] = 255 - image.data[i];
            out.data[i + 3] = 255;
        }
        return out;
    }

    function runPanel() {
        var canvas = document.getElementById("panel-canvas");
        if (!canvas || !canvas.getContext) return;
        var g = canvas.getContext("2d", { willReadFrequently: true });
        var w = canvas.width, h = canvas.height;

        drawScreenshot(g, w, h);
        var settled = dither(g.getImageData(0, 0, w, h));
        var flashed = invert(settled);

        var still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
        if (still) {
            g.putImageData(settled, 0, 0);
            return;
        }

        function fill(v) { g.fillStyle = "rgb(" + v + "," + v + "," + v + ")"; g.fillRect(0, 0, w, h); }

        /* The refresh a Kobo performs when it draws a full-screen image:
         * black, white, the inverse, then the picture. */
        var timeline = [
            [0,   function () { fill(0); }],
            [110, function () { fill(255); }],
            [180, function () { g.putImageData(flashed, 0, 0); }],
            [290, function () { fill(255); }],
            [350, function () { g.putImageData(settled, 0, 0); }]
        ];

        var start = performance.now(), next = 0;
        (function step(now) {
            while (next < timeline.length && now - start >= timeline[next][0]) {
                timeline[next++][1]();
            }
            if (next < timeline.length) requestAnimationFrame(step);
        })(start);
    }

    /* ----------------------------------------------------------- the builder */

    function runBuilder() {
        var form = document.getElementById("plugin-form");
        var status = document.getElementById("build-status");
        if (!form || !status) return;

        if (!window.Blob || !window.URL || !window.URL.createObjectURL) {
            status.textContent = "This browser cannot build files. Use the full bundle above.";
            form.querySelector("#build").disabled = true;
            return;
        }

        form.addEventListener("submit", function (e) {
            e.preventDefault();
            var data = new FormData(form);
            var t0 = performance.now();
            var bytes = KoboTar.buildPluginBundle(KoboPluginFiles, {
                port: data.get("port"),
                poll: data.get("poll"),
                keep: data.get("keep"),
                max_body: Number(data.get("max_body")) * 1024 * 1024,
                save_dir: data.get("save_dir")
            }, fflate.gzipSync);
            var ms = Math.round(performance.now() - t0);

            var url = URL.createObjectURL(new Blob([bytes], { type: "application/gzip" }));
            var a = document.createElement("a");
            a.href = url;
            a.download = "KoboRoot.tgz";
            document.body.appendChild(a);
            a.click();
            a.remove();
            setTimeout(function () { URL.revokeObjectURL(url); }, 10000);

            status.textContent = "Built in " + ms + " ms, " + (bytes.length / 1024).toFixed(1) +
                " KB. Copy it into .kobo, eject, reboot.";
        });
    }

    runPanel();
    runBuilder();
})();

#!/usr/bin/env node
/* Milestone screenshots, plus the two page checks that are cheap to regress:
 * the download control must be above the fold at 360x640, and the page must
 * still make sense with JavaScript off.
 *
 *   node scripts/shoot.mjs <label>        e.g. m2-unstyled, m3-styled
 */
import { chromium } from "playwright";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join, extname, normalize } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const label = process.argv[2] || "current";

/* Served over HTTP, not file://, so the weight measured below is the weight a
 * visitor actually pays. */
const TYPES = { ".html": "text/html", ".css": "text/css", ".js": "text/javascript" };
const server = createServer(async (req, res) => {
    const rel = normalize(decodeURI(req.url.split("?")[0])).replace(/^(\.\.[/\\])+/, "");
    const file = join(root, rel === "/" ? "index.html" : rel);
    try {
        const body = await readFile(file);
        res.writeHead(200, { "content-type": TYPES[extname(file)] || "application/octet-stream" });
        res.end(body);
    } catch {
        res.writeHead(404).end("not found");
    }
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const url = `http://127.0.0.1:${server.address().port}/`;

/* The container ships its own Chromium; PLAYWRIGHT_CHROMIUM points at it when
 * the bundled build number doesn't match. */
const browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_CHROMIUM || undefined
});

async function shoot(name, width, height, opts = {}) {
    const ctx = await browser.newContext({
        viewport: { width, height },
        deviceScaleFactor: 2,
        javaScriptEnabled: opts.js !== false,
        reducedMotion: opts.reducedMotion
    });
    const page = await ctx.newPage();
    await page.goto(url);
    await page.waitForTimeout(opts.settle ?? 900);
    const file = join(root, "screenshots", `${label}-${name}.png`);
    await page.screenshot({ path: file, fullPage: opts.fullPage !== false });
    console.log(`  wrote screenshots/${label}-${name}.png`);
    return { page, ctx };
}

/* 360x640 fold check -- acceptance criterion 4. */
const { page: p360, ctx: c360 } = await shoot("360", 360, 640);
const fold = await p360.evaluate(() => {
    const el = document.querySelector(".hero .button");
    if (!el) return { error: "no download control found" };
    const r = el.getBoundingClientRect();
    return { top: Math.round(r.top), bottom: Math.round(r.bottom), scrollY: window.scrollY };
});
await p360.screenshot({ path: join(root, "screenshots", `${label}-360-fold.png`), fullPage: false });
console.log(`  wrote screenshots/${label}-360-fold.png`);
await c360.close();

/* Copy budget -- acceptance criterion 5. */
const { page: pm, ctx: cm } = await shoot("1280", 1280, 900);
const steps = await pm.evaluate(() =>
    [...document.querySelectorAll(".install .steps > li")].map((li) =>
        li.textContent.trim().replace(/\s+/g, " ")
    )
);
const weight = await pm.evaluate(async () => {
    const urls = [location.href, ...[...document.querySelectorAll("script[src], link[rel=stylesheet]")]
        .map((e) => e.src || e.href)];
    let total = 0;
    for (const u of urls) {
        const r = await fetch(u);
        total += (await r.blob()).size;
    }
    return total;
});
await cm.close();

/* No-JS -- acceptance criterion 6. */
const { page: pn, ctx: cn } = await shoot("360-nojs", 360, 640, { js: false, settle: 200 });
const nojs = await pn.evaluate(() => ({
    steps: document.querySelectorAll(".install .steps > li").length,
    href: document.querySelector(".hero .button")?.getAttribute("href") || null
}));
await cn.close();

await browser.close();
server.close();

const longest = steps.reduce((a, s) => Math.max(a, s.split(/\s+/).length), 0);
console.log("");
console.log(`  download control at y=${fold.top}..${fold.bottom} in a 640px viewport ` +
    `-> ${fold.bottom <= 640 ? "above the fold" : "BELOW THE FOLD"}`);
console.log(`  install steps: ${steps.length} (limit 6), longest ${longest} words (limit 25)`);
steps.forEach((s, i) => console.log(`    ${i + 1}. [${String(s.split(/\s+/).length).padStart(2)}w] ${s}`));
console.log(`  page weight, HTML + CSS + JS: ${(weight / 1024).toFixed(1)} KB uncompressed`);
console.log(`  no-JS: ${nojs.steps} steps present, full bundle href ${nojs.href ? "present" : "MISSING"}`);

const problems = [];
if (fold.bottom > 640) problems.push("download control is below the fold at 360x640");
if (steps.length > 6) problems.push(`${steps.length} install steps, limit is 6`);
if (longest > 25) problems.push(`longest step is ${longest} words, limit is 25`);
if (nojs.steps !== steps.length || !nojs.href) problems.push("page degrades badly without JavaScript");
if (problems.length) {
    console.error("\nFAIL: " + problems.join("; "));
    process.exit(1);
}
console.log("\nAll page checks passed.");

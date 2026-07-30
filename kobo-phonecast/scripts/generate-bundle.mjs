#!/usr/bin/env node
/* Produces a plugin bundle outside the browser, using the same assets/tar.js
 * and the same vendored fflate the page loads. Nothing here reimplements the
 * writer — if this file and the page ever disagree, this file is wrong.
 *
 *   node scripts/generate-bundle.mjs out.tgz [port] [poll] [keep] [max_body] [save_dir]
 */
import { writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { dirname, join, resolve } from "node:path";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const require = createRequire(import.meta.url);

const fflate = require(join(root, "assets", "fflate.min.js"));
const KoboTar = require(join(root, "assets", "tar.js"));
const KoboPluginFiles = require(join(root, "assets", "plugin-files.js"));

const [out, port, poll, keep, maxBody, saveDir] = process.argv.slice(2);
if (!out) {
    console.error("usage: generate-bundle.mjs out.tgz [port] [poll] [keep] [max_body] [save_dir]");
    process.exit(1);
}

const bytes = KoboTar.buildPluginBundle(
    KoboPluginFiles,
    {
        port: port ?? 8080,
        poll: poll ?? 1,
        keep: keep ?? 30,
        max_body: maxBody ?? 32 * 1024 * 1024,
        save_dir: saveDir ?? ""
    },
    fflate.gzipSync
);

writeFileSync(resolve(out), bytes);
console.log(`${out}: ${bytes.length} bytes`);

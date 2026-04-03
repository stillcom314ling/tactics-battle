import { dotnet } from './_framework/dotnet.js'

// Raylib WASM looks for a <canvas id="canvas"> element
const canvas = document.createElement('canvas');
canvas.id = 'canvas';
canvas.width = 800;
canvas.height = 450;
canvas.oncontextmenu = e => e.preventDefault();
document.body.style.cssText = 'margin:0;background:#1a1a2e;display:flex;justify-content:center;align-items:center;height:100vh';
document.body.appendChild(canvas);

await dotnet
    .withDiagnosticTracing(false)
    .run();

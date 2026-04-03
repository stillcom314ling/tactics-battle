import { dotnet } from './dotnet.js'

// Create canvas for Raylib before initialising the runtime
const canvas = document.createElement('canvas');
canvas.id = 'canvas';
canvas.width = 800;
canvas.height = 450;
canvas.oncontextmenu = e => e.preventDefault();
Object.assign(document.body.style, {
    margin: '0', background: '#1a1a2e',
    display: 'flex', justifyContent: 'center',
    alignItems: 'center', height: '100vh'
});
document.body.appendChild(canvas);

const { getAssemblyExports, getConfig } = await dotnet
    .withDiagnosticTracing(false)
    .create();

const config = getConfig();
const exports = await getAssemblyExports(config.mainAssemblyName);

// Main() sets up the Raylib window; DrawFrame() is called each browser frame
exports.RaylibGame.Program.Main();

function loop() {
    exports.RaylibGame.Program.DrawFrame();
    requestAnimationFrame(loop);
}
requestAnimationFrame(loop);

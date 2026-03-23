import { Game } from './game/Game';

async function main() {
  const container = document.getElementById('game-container')!;
  const game = new Game(container);
  await game.init();
  game.start();
}

main().catch(console.error);

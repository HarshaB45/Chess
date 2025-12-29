const pieceMap = {
  'K':'♔','Q':'♕','R':'♖','B':'♗','N':'♘','P':'♙',
  'k':'♚','q':'♛','r':'♜','b':'♝','n':'♞','p':'♟︎','.':' '
};

let positions = []; // array of board arrays
let current = 0;

function renderIndex(idx) {
  const boardEl = document.getElementById('board');
  boardEl.innerHTML = '';
  if (!positions || positions.length === 0) return;
  const board = positions[idx];
  for (let rank = 7; rank >= 0; --rank) {
    for (let file = 0; file < 8; ++file) {
      const i = rank*8 + file;
      const sq = document.createElement('div');
      sq.className = 'square ' + (((rank+file)%2) ? 'dark':'light');
      sq.dataset.square = String.fromCharCode(97+file) + (rank+1);
      const p = board[i];
      sq.textContent = pieceMap[p] || '';
      boardEl.appendChild(sq);
    }
  }
  document.getElementById('moveLabel').textContent = `Move: ${idx} / ${positions.length-1}`;
}

async function loadGame() {
  try {
    const res = await fetch('game.json', {cache:'no-store'});
    if (!res.ok) return;
    const data = await res.json();
    if (!data.positions) return;
    // positions is array of arrays
    positions = data.positions;
    if (current >= positions.length) current = positions.length - 1;
    renderIndex(current);
  } catch (e) {
    console.error('failed to load game.json', e);
  }
}

document.getElementById('prev').addEventListener('click', () => {
  if (current > 0) { current--; renderIndex(current); }
});
document.getElementById('next').addEventListener('click', () => {
  if (current < positions.length - 1) { current++; renderIndex(current); }
});

// keyboard support
document.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowLeft') document.getElementById('prev').click();
  if (e.key === 'ArrowRight') document.getElementById('next').click();
});

// poll game.json in case the C++ program writes new positions
loadGame();
setInterval(loadGame, 1000);

#include <iostream>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <cmath>
#include <filesystem>
#include <ctime>
#include <cstdio>
using Board = std::array<char, 64>;

struct Move
{
    int from;
    int to;
    bool isCapture;
    char promotion; // 'Q', 'R', 'B', 'N' or '\0'
};

struct GameState
{
    Board board;
    bool whiteCastleK = true;
    bool whiteCastleQ = true;
    bool blackCastleK = true;
    bool blackCastleQ = true;
    int enPassant = -1; // square index that can be captured into, or -1
};

// Helpers
bool isWhite(char p) { return p >= 'A' && p <= 'Z'; }
bool isBlack(char p) { return p >= 'a' && p <= 'z'; }

void addMove(std::vector<Move> &moves, int from, int to, bool isCapture = false, char promotion = '\0')
{
    moves.push_back({from, to, isCapture, promotion});
}

std::string squareName(int i)
{
    char file = 'a' + (i % 8);
    char rank = '1' + (i / 8);
    return std::string() + file + rank;
}

void printBoard(const Board &board)
{
    for (int rank = 7; rank >= 0; --rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            std::cout << board[rank * 8 + file] << ' ';
        }
        std::cout << std::endl;
    }
}

// Write a simple JSON file representing the board for the web UI
void writeBoardJson(const GameState &gs, const std::string &path = "web/board.json")
{
    std::ofstream f(path);
    if (!f)
        return;
    f << "{\"board\": [";
    for (int i = 0; i < 64; ++i)
    {
        char c = gs.board[i];
        if (c == '"')
            c = '\\"';
        f << '\"' << c << '\"';
        if (i < 63)
            f << ',';
    }
    f << "], \"enPassant\": " << gs.enPassant << " }";
    f.close();
}

// Write full game positions as JSON array of boards for the web UI
void writeGameJson(const std::vector<Board> &positions, const std::string &path = "web/game.json")
{
    std::ofstream f(path);
    if (!f)
        return;
    f << "{\"positions\": [";
    for (size_t p = 0; p < positions.size(); ++p)
    {
        f << '[';
        for (int i = 0; i < 64; ++i)
        {
            char c = positions[p][i];
            if (c == '"')
                c = '\\"';
            f << '"' << c << '"';
            if (i < 63)
                f << ',';
        }
        f << ']';
        if (p + 1 < positions.size())
            f << ',';
    }
    f << "] }";
    f.close();
}

// Is square attacked by side 'byWhite'
bool isSquareAttacked(const Board &board, int sq, bool byWhite)
{
    int f = sq % 8;
    int r = sq / 8;
    // pawn attacks
    if (byWhite)
    {
        int rf = r - 1;
        if (rf >= 0)
        {
            if (f - 1 >= 0 && board[rf * 8 + (f - 1)] == 'P')
                return true;
            if (f + 1 <= 7 && board[rf * 8 + (f + 1)] == 'P')
                return true;
        }
    }
    else
    {
        int rf = r + 1;
        if (rf <= 7)
        {
            if (f - 1 >= 0 && board[rf * 8 + (f - 1)] == 'p')
                return true;
            if (f + 1 <= 7 && board[rf * 8 + (f + 1)] == 'p')
                return true;
        }
    }

    // knights
    static const int ndf[] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int ndr[] = {2, 1, -1, -2, -2, -1, 1, 2};
    for (int k = 0; k < 8; ++k)
    {
        int nf = f + ndf[k];
        int nr = r + ndr[k];
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7)
            continue;
        char c = board[nr * 8 + nf];
        if (byWhite && c == 'N')
            return true;
        if (!byWhite && c == 'n')
            return true;
    }

    // sliding rooks/queens
    const std::pair<int, int> orth[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto [df, dr] : orth)
    {
        int nf = f + df, nr = r + dr;
        while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7)
        {
            char c = board[nr * 8 + nf];
            if (c != '.')
            {
                if (byWhite && (c == 'R' || c == 'Q'))
                    return true;
                if (!byWhite && (c == 'r' || c == 'q'))
                    return true;
                break;
            }
            nf += df;
            nr += dr;
        }
    }

    // sliding bishops/queens
    const std::pair<int, int> diag[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (auto [df, dr] : diag)
    {
        int nf = f + df, nr = r + dr;
        while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7)
        {
            char c = board[nr * 8 + nf];
            if (c != '.')
            {
                if (byWhite && (c == 'B' || c == 'Q'))
                    return true;
                if (!byWhite && (c == 'b' || c == 'q'))
                    return true;
                break;
            }
            nf += df;
            nr += dr;
        }
    }

    // king
    for (int df = -1; df <= 1; ++df)
        for (int dr = -1; dr <= 1; ++dr)
        {
            if (df == 0 && dr == 0)
                continue;
            int nf = f + df, nr = r + dr;
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7)
                continue;
            char c = board[nr * 8 + nf];
            if (byWhite && c == 'K')
                return true;
            if (!byWhite && c == 'k')
                return true;
        }

    return false;
}

int findKingSquare(const Board &board, bool white)
{
    char K = white ? 'K' : 'k';
    for (int i = 0; i < 64; ++i)
        if (board[i] == K)
            return i;
    return -1;
}

// Create a key for repetition detection: board + castling rights + enPassant + side to move
std::string positionKey(const GameState &gs, bool whiteTurn)
{
    std::string k;
    k.reserve(80);
    for (int i = 0; i < 64; ++i)
        k.push_back(gs.board[i]);
    k.push_back('|');
    k.push_back(gs.whiteCastleK ? 'K' : '-');
    k.push_back(gs.whiteCastleQ ? 'Q' : '-');
    k.push_back(gs.blackCastleK ? 'k' : '-');
    k.push_back(gs.blackCastleQ ? 'q' : '-');
    k.push_back('|');
    if (gs.enPassant >= 0)
    {
        int ep = gs.enPassant;
        k.push_back('0' + (ep % 8));
        k.push_back('0' + (ep / 8));
    }
    else
    {
        k.push_back('-');
        k.push_back('-');
    }
    k.push_back('|');
    k.push_back(whiteTurn ? 'w' : 'b');
    return k;
}

// Evaluation: material only
int evaluate(const Board &board)
{
    int whiteScore = 0, blackScore = 0;
    for (char p : board)
    {
        switch (p)
        {
        case 'P':
            whiteScore += 1;
            break;
        case 'N':
        case 'B':
            whiteScore += 3;
            break;
        case 'R':
            whiteScore += 5;
            break;
        case 'Q':
            whiteScore += 9;
            break;
        case 'p':
            blackScore += 1;
            break;
        case 'n':
        case 'b':
            blackScore += 3;
            break;
        case 'r':
            blackScore += 5;
            break;
        case 'q':
            blackScore += 9;
            break;
        default:
            break;
        }
    }
    std::cout << "White Material: " << whiteScore << "  Black Material: " << blackScore << std::endl;
    return whiteScore - blackScore;
}

// material balance (white - black) without debug output
int materialBalance(const Board &board)
{
    int whiteScore = 0, blackScore = 0;
    for (char p : board)
    {
        switch (p)
        {
        case 'P':
            whiteScore += 1;
            break;
        case 'N':
        case 'B':
            whiteScore += 3;
            break;
        case 'R':
            whiteScore += 5;
            break;
        case 'Q':
            whiteScore += 9;
            break;
        case 'p':
            blackScore += 1;
            break;
        case 'n':
        case 'b':
            blackScore += 3;
            break;
        case 'r':
            blackScore += 5;
            break;
        case 'q':
            blackScore += 9;
            break;
        default:
            break;
        }
    }
    return whiteScore - blackScore;
}

// Minimal move application (ignores castling/en passant for simplicity)
GameState applyMove(const GameState &gs, const Move &m)
{
    GameState ng = gs;
    char piece = ng.board[m.from];
    // reset enPassant unless set below
    ng.enPassant = -1;

    // en passant capture
    if ((piece == 'P' || piece == 'p') && m.isCapture && m.to == gs.enPassant)
    {
        if (piece == 'P')
            ng.board[m.to - 8] = '.'; // capture black pawn
        else
            ng.board[m.to + 8] = '.'; // capture white pawn
    }

    // move piece / promotion
    ng.board[m.to] = (m.promotion != '\0') ? m.promotion : piece;
    ng.board[m.from] = '.';

    // pawn double move -> set enPassant
    if (piece == 'P' && m.to - m.from == 16)
        ng.enPassant = m.from + 8;
    else if (piece == 'p' && m.from - m.to == 16)
        ng.enPassant = m.from - 8;

    // castling: if king moved two squares, move rook
    if ((piece == 'K' || piece == 'k') && abs((m.to % 8) - (m.from % 8)) == 2)
    {
        if (piece == 'K')
        {
            if (m.to % 8 == 6)
            { // white kingside
                ng.board[5] = 'R';
                ng.board[7] = '.';
            }
            else
            { // white queenside
                ng.board[3] = 'R';
                ng.board[0] = '.';
            }
            ng.whiteCastleK = ng.whiteCastleQ = false;
        }
        else
        {
            if (m.to % 8 == 6)
            { // black kingside (squares 61/62)
                ng.board[61] = 'r';
                ng.board[63] = '.';
            }
            else
            {
                ng.board[59] = 'r';
                ng.board[56] = '.';
            }
            ng.blackCastleK = ng.blackCastleQ = false;
        }
    }

    // update castling rights if king or rook moved/captured
    if (m.from == 4 || m.to == 4)
    {
        ng.whiteCastleK = ng.whiteCastleQ = false;
    }
    if (m.from == 60 || m.to == 60)
    {
        ng.blackCastleK = ng.blackCastleQ = false;
    }
    if (m.from == 0 || m.to == 0)
        ng.whiteCastleQ = false;
    if (m.from == 7 || m.to == 7)
        ng.whiteCastleK = false;
    if (m.from == 56 || m.to == 56)
        ng.blackCastleQ = false;
    if (m.from == 63 || m.to == 63)
        ng.blackCastleK = false;

    return ng;
}

// --- Pawn + piece move generators (simplified) ---
int fileOf(int idx) { return idx % 8; }
int rankOf(int idx) { return idx / 8; }
bool sameColor(char a, char b)
{
    if (a == '.' || b == '.')
        return false;
    return (isWhite(a) && isWhite(b)) || (isBlack(a) && isBlack(b));
}

void generatePawnMoves(const GameState &gs, bool whiteTurn, std::vector<Move> &moves)
{
    const Board &board = gs.board;
    for (int i = 0; i < 64; ++i)
    {
        if (whiteTurn && board[i] == 'P')
        {
            // Forward 1
            if (i + 8 < 64 && board[i + 8] == '.')
            {
                // promotion
                if (i + 8 >= 56)
                {
                    addMove(moves, i, i + 8, false, 'Q');
                    addMove(moves, i, i + 8, false, 'R');
                    addMove(moves, i, i + 8, false, 'B');
                    addMove(moves, i, i + 8, false, 'N');
                }
                else
                    addMove(moves, i, i + 8);
                // Forward 2
                if (i / 8 == 1 && board[i + 16] == '.')
                    addMove(moves, i, i + 16);
            }
            // Captures
            if (i % 8 != 0)
            {
                if (i + 7 < 64 && isBlack(board[i + 7]))
                {
                    if (i + 7 >= 56)
                    {
                        addMove(moves, i, i + 7, true, 'Q');
                        addMove(moves, i, i + 7, true, 'R');
                        addMove(moves, i, i + 7, true, 'B');
                        addMove(moves, i, i + 7, true, 'N');
                    }
                    else
                        addMove(moves, i, i + 7, true);
                }
                // en passant
                if (gs.enPassant == i + 7)
                    addMove(moves, i, i + 7, true);
            }
            if (i % 8 != 7)
            {
                if (i + 9 < 64 && isBlack(board[i + 9]))
                {
                    if (i + 9 >= 56)
                    {
                        addMove(moves, i, i + 9, true, 'Q');
                        addMove(moves, i, i + 9, true, 'R');
                        addMove(moves, i, i + 9, true, 'B');
                        addMove(moves, i, i + 9, true, 'N');
                    }
                    else
                        addMove(moves, i, i + 9, true);
                }
                if (gs.enPassant == i + 9)
                    addMove(moves, i, i + 9, true);
            }
        }
        else if (!whiteTurn && board[i] == 'p')
        {
            // Forward 1
            if (i - 8 >= 0 && board[i - 8] == '.')
            {
                if (i - 8 <= 7)
                {
                    addMove(moves, i, i - 8, false, 'q');
                    addMove(moves, i, i - 8, false, 'r');
                    addMove(moves, i, i - 8, false, 'b');
                    addMove(moves, i, i - 8, false, 'n');
                }
                else
                    addMove(moves, i, i - 8);
                // Forward 2
                if (i / 8 == 6 && board[i - 16] == '.')
                    addMove(moves, i, i - 16);
            }
            // Captures
            if (i % 8 != 0)
            {
                if (i - 9 >= 0 && isWhite(board[i - 9]))
                {
                    if (i - 9 <= 7)
                    {
                        addMove(moves, i, i - 9, true, 'q');
                        addMove(moves, i, i - 9, true, 'r');
                        addMove(moves, i, i - 9, true, 'b');
                        addMove(moves, i, i - 9, true, 'n');
                    }
                    else
                        addMove(moves, i, i - 9, true);
                }
                if (gs.enPassant == i - 9)
                    addMove(moves, i, i - 9, true);
            }
            if (i % 8 != 7)
            {
                if (i - 7 >= 0 && isWhite(board[i - 7]))
                {
                    if (i - 7 <= 7)
                    {
                        addMove(moves, i, i - 7, true, 'q');
                        addMove(moves, i, i - 7, true, 'r');
                        addMove(moves, i, i - 7, true, 'b');
                        addMove(moves, i, i - 7, true, 'n');
                    }
                    else
                        addMove(moves, i, i - 7, true);
                }
                if (gs.enPassant == i - 7)
                    addMove(moves, i, i - 7, true);
            }
        }
    }
}

void generateKnightMoves(const Board &board, int i, bool whiteTurn, std::vector<Move> &moves)
{
    static const int dfile[] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int drank[] = {2, 1, -1, -2, -2, -1, 1, 2};
    char me = board[i];
    for (int k = 0; k < 8; ++k)
    {
        int nf = fileOf(i) + dfile[k], nr = rankOf(i) + drank[k];
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7)
            continue;
        int to = nr * 8 + nf;
        if (board[to] == '.')
            addMove(moves, i, to);
        else if (!sameColor(me, board[to]))
            addMove(moves, i, to, true);
    }
}

void generateSlidingMoves(const Board &board, int i, const std::vector<std::pair<int, int>> &dirs, std::vector<Move> &moves)
{
    char me = board[i];
    for (auto [df, dr] : dirs)
    {
        int nf = fileOf(i) + df, nr = rankOf(i) + dr;
        while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7)
        {
            int to = nr * 8 + nf;
            if (board[to] == '.')
                addMove(moves, i, to);
            else
            {
                if (!sameColor(me, board[to]))
                    addMove(moves, i, to, true);
                break;
            }
            nf += df;
            nr += dr;
        }
    }
}

void generateKingMoves(const GameState &gs, int i, std::vector<Move> &moves)
{
    const Board &board = gs.board;
    char me = board[i];
    bool white = isWhite(me);
    for (int df = -1; df <= 1; ++df)
        for (int dr = -1; dr <= 1; ++dr)
        {
            if (df == 0 && dr == 0)
                continue;
            int nf = fileOf(i) + df, nr = rankOf(i) + dr;
            if (nf < 0 || nf > 7 || nr < 0 || nr > 7)
                continue;
            int to = nr * 8 + nf;
            if (board[to] == '.')
                addMove(moves, i, to);
            else if (!sameColor(me, board[to]))
                addMove(moves, i, to, true);
        }

    // Castling pseudo-legal (squares must be empty and not attacked)
    if (white && me == 'K' && i == 4)
    {
        // kingside
        if (gs.whiteCastleK && board[5] == '.' && board[6] == '.')
        {
            if (!isSquareAttacked(board, 4, false) && !isSquareAttacked(board, 5, false) && !isSquareAttacked(board, 6, false))
                addMove(moves, 4, 6, false);
        }
        // queenside
        if (gs.whiteCastleQ && board[3] == '.' && board[2] == '.' && board[1] == '.')
        {
            if (!isSquareAttacked(board, 4, false) && !isSquareAttacked(board, 3, false) && !isSquareAttacked(board, 2, false))
                addMove(moves, 4, 2, false);
        }
    }
    else if (!white && me == 'k' && i == 60)
    {
        if (gs.blackCastleK && board[61] == '.' && board[62] == '.')
        {
            if (!isSquareAttacked(board, 60, true) && !isSquareAttacked(board, 61, true) && !isSquareAttacked(board, 62, true))
                addMove(moves, 60, 62, false);
        }
        if (gs.blackCastleQ && board[59] == '.' && board[58] == '.' && board[57] == '.')
        {
            if (!isSquareAttacked(board, 60, true) && !isSquareAttacked(board, 59, true) && !isSquareAttacked(board, 58, true))
                addMove(moves, 60, 58, false);
        }
    }
}

void generateAllMoves(const GameState &gs, bool whiteTurn, std::vector<Move> &moves)
{
    const Board &board = gs.board;
    for (int i = 0; i < 64; ++i)
    {
        char p = board[i];
        if (p == '.')
            continue;
        if (whiteTurn && !isWhite(p))
            continue;
        if (!whiteTurn && !isBlack(p))
            continue;
        switch (p)
        {
        case 'N':
        case 'n':
            generateKnightMoves(board, i, whiteTurn, moves);
            break;
        case 'B':
        case 'b':
            generateSlidingMoves(board, i, {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}, moves);
            break;
        case 'R':
        case 'r':
            generateSlidingMoves(board, i, {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}, moves);
            break;
        case 'Q':
        case 'q':
            generateSlidingMoves(board, i, {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}, moves);
            break;
        case 'K':
        case 'k':
            generateKingMoves(gs, i, moves);
            break;
        default:
            break;
        }
    }
}

void generateLegalMoves(const GameState &gs, bool whiteTurn, std::vector<Move> &legal)
{
    std::vector<Move> pseudo;
    generatePawnMoves(gs, whiteTurn, pseudo);
    generateAllMoves(gs, whiteTurn, pseudo);
    for (auto &m : pseudo)
    {
        GameState ng = applyMove(gs, m);
        int kingSq = findKingSquare(ng.board, whiteTurn);
        if (kingSq == -1)
            continue;
        // if king is attacked by opponent after move, it's illegal
        if (isSquareAttacked(ng.board, kingSq, !whiteTurn))
            continue;
        legal.push_back(m);
    }
}

// Check whether making move `m` from `gs` allows an immediate opponent capture on m.to
// that results in a material swing <= threshold (from mover's perspective).
bool allowsBadImmediateRecapture(const GameState &gs, const Move &m, bool whiteTurn, int threshold)
{
    GameState ng = applyMove(gs, m);
    bool oppWhite = !whiteTurn;
    std::vector<Move> oppMoves;
    generateLegalMoves(ng, oppWhite, oppMoves);
    int before = materialBalance(gs.board);
    for (auto &r : oppMoves)
    {
        if (!r.isCapture)
            continue;
        if (r.to != m.to)
            continue;
        GameState ng2 = applyMove(ng, r);
        int after = materialBalance(ng2.board);
        int deltaWhite = after - before;
        int deltaForMover = whiteTurn ? deltaWhite : -deltaWhite;
        if (deltaForMover <= threshold)
            return true;
    }
    return false;
}

// Aggressive evaluation: only counts capture opportunities and center control for side to move.
int evaluateAggressive(const GameState &gs, bool whiteTurn)
{
    std::vector<Move> moves;
    generateLegalMoves(gs, whiteTurn, moves);
    int captureCount = 0;
    for (auto &m : moves)
        if (m.isCapture)
            ++captureCount;

    // center squares: d4,e4,d5,e5 -> indices 27,28,35,36
    const int centerIdx[4] = {27, 28, 35, 36};
    int centerControl = 0;
    for (int ci = 0; ci < 4; ++ci)
    {
        char c = gs.board[centerIdx[ci]];
        if (c == '.')
            continue;
        if (whiteTurn && isWhite(c))
            centerControl += 1;
        if (!whiteTurn && isBlack(c))
            centerControl += 1;
    }

    // score oriented to side-to-move (higher is better)
    return captureCount * 800 + centerControl * 120;
}

// Move ordering heuristic: prefer captures, then pawn double pushes on c/d/e, then center moves
// forward declare helper used by moveHeuristic
bool squareAttackedByAfterMove(const GameState &gs, const Move &m, bool byWhite);

int moveHeuristic(const GameState &gs, const Move &m)
{
    int score = 0;
    if (m.isCapture)
    {
        score += 20000; // huge priority for captures
        // penalize captures that leave the capturing piece on a square defended by the opponent
        if (squareAttackedByAfterMove(gs, m, !isWhite(gs.board[m.from])))
            score -= 15000; // discourage capturing a defended piece
    }

    // center target bonus
    if (m.to == 27 || m.to == 28 || m.to == 35 || m.to == 36)
        score += 500;

    // prefer two-step pawn pushes on files c(2)/d(3)/e(4)
    int fromFile = m.from % 8;
    if (abs(m.to - m.from) == 16)
    {
        // verify it's a pawn by checking piece on source in current gs
        char piece = gs.board[m.from];
        if (piece == 'P' || piece == 'p')
        {
            if (fromFile == 2 || fromFile == 3 || fromFile == 4)
                score += 5000; // very desirable
        }
    }
    return score;
}

// Determine if the square 'sq' is attacked by side 'byWhite' (wrapper of isSquareAttacked)
bool squareAttackedByAfterMove(const GameState &gs, const Move &m, bool byWhite)
{
    GameState ng = applyMove(gs, m);
    return isSquareAttacked(ng.board, m.to, byWhite);
}

int negamax(const GameState &gs, bool whiteTurn, int depth, int alpha, int beta)
{
    if (depth == 0)
        return evaluateAggressive(gs, whiteTurn);

    std::vector<Move> moves;
    generateLegalMoves(gs, whiteTurn, moves);
    if (moves.empty())
    {
        int kingSq = findKingSquare(gs.board, whiteTurn);
        bool inCheck = (kingSq != -1) && isSquareAttacked(gs.board, kingSq, !whiteTurn);
        if (inCheck)
            return -100000 + (3 - depth); // checkmate worse for side to move
        else
            return 0; // stalemate
    }

    // order moves by heuristic descending
    std::sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b)
              { return moveHeuristic(gs, a) > moveHeuristic(gs, b); });

    int best = -1000000;
    for (auto &m : moves)
    {
        GameState ng = applyMove(gs, m);
        // avoid immediate large material loss in deeper search as well
        int deltaWhite = materialBalance(ng.board) - materialBalance(gs.board);
        int deltaForSide = whiteTurn ? deltaWhite : -deltaWhite;
        if (deltaForSide <= -4)
            continue;
        if (allowsBadImmediateRecapture(gs, m, whiteTurn, -4))
            continue;

        int val = -negamax(ng, !whiteTurn, depth - 1, -beta, -alpha);
        if (val > best)
            best = val;
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;
    }
    return best;
}

Move searchBestMove(const GameState &gs, bool whiteTurn, int depth)
{
    std::vector<Move> moves;
    generateLegalMoves(gs, whiteTurn, moves);
    if (moves.empty())
        return {0, 0, false, '\0'};

    // order moves by heuristic
    std::sort(moves.begin(), moves.end(), [&](const Move &a, const Move &b)
              { return moveHeuristic(gs, a) > moveHeuristic(gs, b); });
    // avoid immediate large material loss: threshold is material points (side-perspective)
    const int materialLossThreshold = -4; // disallow moves that immediately lose >= 4 points
    int skipped = 0;
    Move bestMove = moves[0];
    int alpha = -1000000, beta = 1000000;
    for (auto &m : moves)
    {
        GameState ng = applyMove(gs, m);
        int deltaWhite = materialBalance(ng.board) - materialBalance(gs.board);
        int deltaForSide = whiteTurn ? deltaWhite : -deltaWhite;
        if (deltaForSide <= materialLossThreshold)
        {
            ++skipped;
            continue;
        }
        // also check for immediate recapture by opponent that causes large loss
        if (allowsBadImmediateRecapture(gs, m, whiteTurn, materialLossThreshold))
        {
            ++skipped;
            continue;
        }
        int val = -negamax(ng, !whiteTurn, depth - 1, -beta, -alpha);
        if (val > alpha)
        {
            alpha = val;
            bestMove = m;
        }
    }
    // if we skipped all moves, allow them (no legal safe move)
    if (skipped == (int)moves.size())
    {
        alpha = -1000000;
        for (auto &m : moves)
        {
            GameState ng = applyMove(gs, m);
            int val = -negamax(ng, !whiteTurn, depth - 1, -beta, -alpha);
            if (val > alpha)
            {
                alpha = val;
                bestMove = m;
            }
        }
    }
    return bestMove;
}

int main()
{
    // Setup board
    Board board{};
    for (int i = 0; i < 64; i++)
        board[i] = '.';
    for (int i = 0; i < 8; i++)
    {
        board[1 * 8 + i] = 'P';
        board[6 * 8 + i] = 'p';
        if (i == 0 || i == 7)
        {
            board[0 * 8 + i] = 'R';
            board[7 * 8 + i] = 'r';
        }
        else if (i == 1 || i == 6)
        {
            board[0 * 8 + i] = 'N';
            board[7 * 8 + i] = 'n';
        }
        else if (i == 2 || i == 5)
        {
            board[0 * 8 + i] = 'B';
            board[7 * 8 + i] = 'b';
        }
        else if (i == 3)
        {
            board[0 * 8 + i] = 'Q';
            board[7 * 8 + i] = 'q';
        }
        else if (i == 4)
        {
            board[0 * 8 + i] = 'K';
            board[7 * 8 + i] = 'k';
        }
    }

    GameState gs;
    gs.board = board;

    // write initial board JSON for web UI and start a positions history
    std::vector<Board> positions;
    positions.push_back(gs.board);
    writeBoardJson(gs);
    writeGameJson(positions);
    // give the web UI time to load the initial position
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    printBoard(gs.board);

    bool whiteTurn = true;
    const int maxPlies = 1000; // safety cap to avoid infinite loops
    int turn = 0;
    int halfmoveClock = 0; // halfmoves since last pawn move or capture
    std::unordered_map<std::string, int> repetitionCount;
    // record initial position
    repetitionCount[positionKey(gs, whiteTurn)] = 1;

    std::vector<std::string> pgnMoves;
    std::string gameResult = "*";

    for (; turn < maxPlies; ++turn)
    {
        std::vector<Move> legal;
        generateLegalMoves(gs, whiteTurn, legal);
        if (legal.empty())
        {
            int kingSq = findKingSquare(gs.board, whiteTurn);
            bool inCheck = (kingSq != -1) && isSquareAttacked(gs.board, kingSq, !whiteTurn);
            if (inCheck)
            {
                std::cout << (whiteTurn ? "White" : "Black") << " is checkmated!\n";
                gameResult = whiteTurn ? "0-1" : "1-0";
            }
            else
            {
                std::cout << (whiteTurn ? "White" : "Black") << " has no legal moves (stalemate)!\n";
                gameResult = "1/2-1/2";
            }
            break;
        }

        // pick best move using negamax alpha-beta with aggressive priorities
        const int searchDepth = 3; // tune depth as desired
        Move bestMove = searchBestMove(gs, whiteTurn, searchDepth);

        std::cout << "\n"
                  << (whiteTurn ? "White" : "Black") << " plays: " << squareName(bestMove.from) << " -> " << squareName(bestMove.to) << "\n";

        // SAN generation (simple): castling, piece letter, captures, promotions, check/mate marker
        auto moveToSAN = [&](const GameState &curGs, const Move &m, bool curWhite) -> std::string
        {
            char piece = curGs.board[m.from];
            // castling
            if ((piece == 'K' || piece == 'k') && abs((m.to % 8) - (m.from % 8)) == 2)
            {
                if ((m.to % 8) == 6)
                    return std::string("O-O");
                else
                    return std::string("O-O-O");
            }
            std::string san;
            bool isPawn = (piece == 'P' || piece == 'p');
            if (isPawn)
            {
                if (m.isCapture)
                {
                    char file = 'a' + (m.from % 8);
                    san.push_back(file);
                    san.push_back('x');
                }
                san += squareName(m.to);
                if (m.promotion != '\0')
                {
                    san.push_back('=');
                    san.push_back((char)toupper((unsigned char)m.promotion));
                }
            }
            else
            {
                // ensure piece letter is present; if source square somehow empty, attempt to infer
                char up = '?';
                if (piece != '.' && piece != '\0')
                    up = (char)toupper((unsigned char)piece);
                else
                {
                    // fallback: infer by scanning legal pieces that could move to m.from (best-effort)
                    up = 'P';
                }
                san.push_back(up);
                if (m.isCapture)
                    san.push_back('x');
                san += squareName(m.to);
            }

            // check/mate detection
            GameState ng = applyMove(curGs, m);
            bool oppIsWhite = !curWhite;
            int oppKing = findKingSquare(ng.board, oppIsWhite);
            bool inCheck = (oppKing != -1) && isSquareAttacked(ng.board, oppKing, curWhite);
            std::vector<Move> oppMoves;
            generateLegalMoves(ng, oppIsWhite, oppMoves);
            if (inCheck && oppMoves.empty())
                san += '#';
            else if (inCheck)
                san += '+';

            return san;
        };

        std::string san = moveToSAN(gs, bestMove, whiteTurn);

        // update halfmove clock: reset on pawn move or capture
        char movingPiece = gs.board[bestMove.from];
        if (movingPiece == 'P' || movingPiece == 'p' || bestMove.isCapture)
            halfmoveClock = 0;
        else
            ++halfmoveClock;

        // apply move and record SAN
        GameState oldGs = gs;
        gs = applyMove(gs, bestMove);
        pgnMoves.push_back(san);

        // toggle side to move
        whiteTurn = !whiteTurn;

        printBoard(gs.board);
        // update JSON for web UI and pause so browser can display the move
        positions.push_back(gs.board);
        writeBoardJson(gs);
        writeGameJson(positions);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // repetition detection (threefold)
        std::string key = positionKey(gs, whiteTurn);
        int cnt = ++repetitionCount[key];
        if (cnt >= 3)
        {
            std::cout << "Draw by threefold repetition.\n";
            gameResult = "1/2-1/2";
            break;
        }

        // 50-move rule: 100 halfmoves = 50 moves each
        if (halfmoveClock >= 100)
        {
            std::cout << "Draw by 50-move rule.\n";
            gameResult = "1/2-1/2";
            break;
        }
    }

    // write PGN file if we have moves
    if (!pgnMoves.empty())
    {
        std::filesystem::create_directories("pgns");
        int idx = 1;
        std::string base;
        do
        {
            base = "pgns/pgn" + std::to_string(idx) + ".pgn";
            ++idx;
        } while (std::filesystem::exists(base));

        std::ofstream pf(base);
        if (pf)
        {
            // header
            std::time_t t = std::time(nullptr);
            std::tm tm = *std::localtime(&t);
            char datebuf[32];
            std::snprintf(datebuf, sizeof(datebuf), "%04d.%02d.%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            pf << "[Event \"Friendly Game\"]\n";
            pf << "[Site \"Local\"]\n";
            pf << "[Date \"" << datebuf << "\"]\n";
            pf << "[Round \"-\"]\n";
            pf << "[White \"White\"]\n";
            pf << "[Black \"Black\"]\n";
            pf << "[Result \"" << gameResult << "\"]\n\n";

            // movetext
            for (size_t i = 0; i < pgnMoves.size(); ++i)
            {
                if (i % 2 == 0)
                {
                    pf << (i / 2 + 1) << ". ";
                }
                pf << pgnMoves[i];
                if (i % 2 == 1)
                    pf << ' ';
                else
                    pf << ' ';
            }
            pf << " " << gameResult << "\n";
            pf.close();
            std::cout << "Wrote PGN to " << base << "\n";
        }
        else
        {
            std::cout << "Failed to write PGN to " << base << "\n";
        }
    }

    return 0;
}

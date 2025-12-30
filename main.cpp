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

    for (; turn < maxPlies; ++turn)
    {
        std::vector<Move> legal;
        generateLegalMoves(gs, whiteTurn, legal);
        if (legal.empty())
        {
            int kingSq = findKingSquare(gs.board, whiteTurn);
            bool inCheck = (kingSq != -1) && isSquareAttacked(gs.board, kingSq, !whiteTurn);
            if (inCheck)
                std::cout << (whiteTurn ? "White" : "Black") << " is checkmated!\n";
            else
                std::cout << (whiteTurn ? "White" : "Black") << " has no legal moves (stalemate)!\n";
            break;
        }

        // pick best move greedily by material
        int bestScore = whiteTurn ? -1000 : 1000;
        Move bestMove = legal[0];
        for (auto &m : legal)
        {
            GameState ng = applyMove(gs, m);
            int score = evaluate(ng.board);
            if (whiteTurn && score > bestScore)
            {
                bestScore = score;
                bestMove = m;
            }
            if (!whiteTurn && score < bestScore)
            {
                bestScore = score;
                bestMove = m;
            }
        }

        std::cout << "\n"
                  << (whiteTurn ? "White" : "Black") << " plays: " << squareName(bestMove.from) << " -> " << squareName(bestMove.to) << "\n";

        // update halfmove clock: reset on pawn move or capture
        char movingPiece = gs.board[bestMove.from];
        if (movingPiece == 'P' || movingPiece == 'p' || bestMove.isCapture)
            halfmoveClock = 0;
        else
            ++halfmoveClock;

        gs = applyMove(gs, bestMove);
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
            break;
        }

        // 50-move rule: 100 halfmoves = 50 moves each
        if (halfmoveClock >= 100)
        {
            std::cout << "Draw by 50-move rule.\n";
            break;
        }
    }

    return 0;
}

#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <optional>

using namespace std;

//==================================================
// GAME CONSTANTS
//==================================================
const int N    = 7;
const int CELL = 80;        // Pixel size of each cell
const int UI_HEIGHT = 40;   // Extra space at bottom for text
const int DEPTH_LIMIT = 3;  // Minimax depth limit

const int WIN_SCORE  = 1000000;
const int LOSE_SCORE = -1000000;

enum Cell {
    EMPTY   = 0,
    AI_PAWN = 1,   // Blue / AI / MAX
    HU_PAWN = 2,   // Red / Human / MIN
    BLOCKED = -1
};

int dx[8] = { -1,-1,-1, 0, 0, 1, 1, 1 };
int dy[8] = { -1, 0, 1,-1, 1,-1, 0, 1 };

//==================================================
// STATE STRUCTURE
//==================================================
struct State {
    int board[N][N];
    int aiX, aiY;
    int huX, huY;
    bool isMaxTurn;  // true = AI (BLUE / MAX), false = Human (RED / MIN)
};

struct Move {
    int moveX, moveY;
    int removeX, removeY;
};

//==================================================
// Helper Functions
//==================================================
bool inBounds(int x, int y) {
    return (x >= 0 && x < N && y >= 0 && y < N);
}

bool isLegitMove(const State& s, int fx, int fy, int tx, int ty) {
    if (!inBounds(tx, ty)) return false;

    int dx_ = tx - fx;
    int dy_ = ty - fy;

    if (abs(dx_) > 1 || abs(dy_) > 1) return false;
    if (dx_ == 0 && dy_ == 0) return false;

    if (s.board[tx][ty] == BLOCKED) return false;
    if (s.board[tx][ty] == AI_PAWN || s.board[tx][ty] == HU_PAWN) return false;

    return true;
}

void getCurrentPlayerPos(const State& s, int& px, int& py) {
    if (s.isMaxTurn) { px = s.aiX; py = s.aiY; }
    else             { px = s.huX; py = s.huY; }
}

//==================================================
// Legal step moves (1-step moves for current player)
//==================================================
vector<pair<int,int>> getLegalStepMoves(const State& s) {
    vector<pair<int,int>> out;
    int px, py;
    getCurrentPlayerPos(s, px, py);

    for (int k = 0; k < 8; k++) {
        int nx = px + dx[k];
        int ny = py + dy[k];
        if (isLegitMove(s, px, py, nx, ny)) {
            out.push_back({nx, ny});
        }
    }
    return out;
}

//==================================================
// Apply Place barrier
//==================================================
bool placeBarrier(State& s, int x, int y) {
    if (!inBounds(x, y)) return false;
    if (s.board[x][y] != EMPTY) return false;
    s.board[x][y] = BLOCKED;
    return true;
}

//==================================================
// Apply step move (only move the pawn)
//==================================================
void applyStepMove(State& s, int toX, int toY) {
    int px, py;
    getCurrentPlayerPos(s, px, py);

    int pawn = s.isMaxTurn ? AI_PAWN : HU_PAWN;
    s.board[px][py] = EMPTY;
    s.board[toX][toY] = pawn;

    if (s.isMaxTurn) { s.aiX = toX; s.aiY = toY; }
    else             { s.huX = toX; s.huY = toY; }
}

//==================================================
// Apply full move (move + barrier, switch turn)
//  -> Used ONLY for AI.
//==================================================
State applyMove(const State& s, const Move& m) {
    State ns = s;
    applyStepMove(ns, m.moveX, m.moveY);
    placeBarrier(ns, m.removeX, m.removeY);
    ns.isMaxTurn = !s.isMaxTurn;
    return ns;
}

//==================================================
// Move counting / terminal test
//==================================================
bool hasNoMoves(const State& s) {
    return getLegalStepMoves(s).empty();
}

int countMovesForPlayer(const State& s, bool forAI) {
    State tmp = s;
    tmp.isMaxTurn = forAI;
    auto mv = getLegalStepMoves(tmp);
    return (int)mv.size();
}

//==================================================
// 3) h(n) / eval function  ---> h = a_n + b_n + c_n
//    a_n = mobility contribution
//    b_n = barrier effect
//    c_n = reachable area (long-term space advantage)
//==================================================

// a_n: Mobility Difference
int calculateMobility(const State& s) {
    // true: AI (Max), false: Human (Min) varsayımı ile
    int aiM = countMovesForPlayer(s, true);
    int huM = countMovesForPlayer(s, false);

    return (aiM - huM);
}
// b_n: Barrier Effect
int calculateBarriers(const State& s) {
    int blockedAroundAI = 0;
    int blockedAroundHU = 0;

    // --- AI (Max) etrafındaki engeller ---
    {
        State temp = s;
        temp.isMaxTurn = true; 
        int ax, ay;
        getCurrentPlayerPos(temp, ax, ay); // AI pozisyonunu al

        for (int k = 0; k < 8; k++) {
            int nx = ax + dx[k];
            int ny = ay + dy[k];
            if (inBounds(nx, ny) && s.board[nx][ny] == BLOCKED) {
                blockedAroundAI++;
            }
        }
    }

    // --- Human (Min) etrafındaki engeller ---
    {
        State temp = s;
        temp.isMaxTurn = false;
        int hx, hy;
        getCurrentPlayerPos(temp, hx, hy); // Human pozisyonunu al

        for (int k = 0; k < 8; k++) {
            int nx = hx + dx[k];
            int ny = hy + dy[k];
            if (inBounds(nx, ny) && s.board[nx][ny] == BLOCKED) {
                blockedAroundHU++;
            }
        }
    }

    // Strateji: Rakibin etrafı kapalıysa iyi (+), bizim etrafımız kapalıysa kötü (-)
    return (blockedAroundHU - blockedAroundAI);
}
// c_n: Reachable Area (long-term spatial advantage)
// AI reachable cells - Human reachable cells
int countReachable(const State& s, bool forAI) {
    int sx = forAI ? s.aiX : s.huX;
    int sy = forAI ? s.aiY : s.huY;

    int otherX = forAI ? s.huX : s.aiX;
    int otherY = forAI ? s.huY : s.aiY;

    bool visited[N][N] = { false };
    std::vector<std::pair<int,int>> q;

    q.push_back({sx, sy});
    visited[sx][sy] = true;

    int reachableCount = 0;

    for (size_t qi = 0; qi < q.size(); ++qi) {
        auto [x, y] = q[qi];
        reachableCount++;

        for (int k = 0; k < 8; ++k) {
            int nx = x + dx[k];
            int ny = y + dy[k];

            if (!inBounds(nx, ny)) continue;
            if (visited[nx][ny]) continue;

            if (s.board[nx][ny] == BLOCKED) continue;
            if (nx == otherX && ny == otherY) continue;

            visited[nx][ny] = true;
            q.push_back({nx, ny});
        }
    }

    return reachableCount;
}
int calculateAreaControl(const State& s) {
    int aiArea = countReachable(s, true);
    int huArea = countReachable(s, false);

    return (aiArea - huArea);
}

int eval(const State& s) {
    // Terminal state: if the current player has no legal moves, the game ends
    if (hasNoMoves(s)) {
        return s.isMaxTurn ? LOSE_SCORE : WIN_SCORE;
    }

    int a = calculateMobility(s) * 5; // weight = 10
    int b = calculateBarriers(s) * 2; // weight = 2
    int c = calculateAreaControl(s) * 10; // weight = 5

    return a + b + c;
}


//==================================================
// Successor: generate all moves (move + barrier) - for AI
//==================================================
vector<Move> generateAllMoves(const State& s) {
    vector<Move> res;
    auto steps = getLegalStepMoves(s);

    for (auto p : steps) {
        int mx = p.first, my = p.second;

        State after = s;
        applyStepMove(after, mx, my);

        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                if (after.board[i][j] == EMPTY) {
                    Move m{mx, my, i, j};
                    res.push_back(m);
                }
            }
        }
    }

    return res;
}

//==================================================
// 6) Minimax (depth limited, with alpha-beta)
//==================================================
int minimax(const State& s, int depth, int alpha, int beta) {
    // Terminal or depth limit reached
    if (depth == 0 || hasNoMoves(s)) {
        return eval(s);
    }

    auto moves = generateAllMoves(s);
    if (moves.empty()) return eval(s);

    if (s.isMaxTurn) {
        // MAX player (AI)
        int best = -1000000000;

        for (const auto& m : moves) {
            State child = applyMove(s, m);
            int val = minimax(child, depth - 1, alpha, beta);

            best = max(best, val);
            alpha = max(alpha, val);

            // Pruning
            if (beta <= alpha)
                break;
        }

        return best;
    } 
    
    else {
        // MIN player (Human)
        int best = 1000000000;

        for (const auto& m : moves) {
            State child = applyMove(s, m);
            int val = minimax(child, depth - 1, alpha, beta);

            best = min(best, val);
            beta = min(beta, val);

            // Pruning
            if (beta <= alpha)
                break;
        }

        return best;
    }
}

Move findBestMove(const State& s, int depth) {
    auto moves = generateAllMoves(s);
    Move best{};
    int bestVal = -1000000000;

    int alpha = -1000000000;
    int beta  =  1000000000;

    for (const auto& m : moves) {
        State child = applyMove(s, m);
        int val = minimax(child, depth - 1, alpha, beta);

        if (val > bestVal) {
            bestVal = val;
            best = m;
        }

        alpha = max(alpha, val);
    }

    return best;
}


//==================================================
// 5) initializeGame
//==================================================
void initializeGame(State& s) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            s.board[i][j] = EMPTY;

    s.aiX = 0;   s.aiY = 3;
    s.huX = 6;   s.huY = 3;

    s.board[s.aiX][s.aiY] = AI_PAWN;
    s.board[s.huX][s.huY] = HU_PAWN;

    s.isMaxTurn = false;  // Human starts
}

//==================================================
// GUI: Board drawing
//==================================================
void drawBoard(sf::RenderWindow& win, const State& s) {
    sf::RectangleShape cell(sf::Vector2f((float)CELL - 2, (float)CELL - 2));

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            int v = s.board[i][j];

            if (v == EMPTY)
                cell.setFillColor(sf::Color(180, 180, 180));
            else if (v == BLOCKED)
                cell.setFillColor(sf::Color::Black);
            else if (v == AI_PAWN)
                cell.setFillColor(sf::Color::Blue);
            else if (v == HU_PAWN)
                cell.setFillColor(sf::Color::Red);

            cell.setPosition(sf::Vector2f((float)(j * CELL + 2), (float)(i * CELL + 2)));
            win.draw(cell);
        }
    }
}

//==================================================
// MAIN
//==================================================
int main() {
    sf::RenderWindow window(
        sf::VideoMode({ (unsigned int)(N * CELL),
                        (unsigned int)(N * CELL + UI_HEIGHT) }),
        "AI Minimax Game"
    );

    // ---------------------------------------------------------
    // LOAD FONT
    // ---------------------------------------------------------
    sf::Font font;
    // Try macOS system font or local file
    if (!font.openFromFile("arial.ttf")) {
        if (!font.openFromFile("/Library/Fonts/Arial.ttf")) {
            std::cout << "Warning: Font not found, text will not be visible.\n";
        }
    }

    sf::Text infoText(font, "", 20);
    infoText.setFillColor(sf::Color::White);
    infoText.setPosition(sf::Vector2f(5.f, (float)(N * CELL + 5)));

    State game;
    initializeGame(game);

    int depthLimit = DEPTH_LIMIT;

    // Human move stage:
    // hStage = 0 -> first select the tile to move to
    // hStage = 1 -> then select the tile to place barrier
    int hStage = 0;

    while (window.isOpen()) {

        // ==========================================================
        // 1. INPUT: User Clicks
        // ==========================================================
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }

            // Listen for clicks only if it's HUMAN's turn
            if (!game.isMaxTurn) {
                if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                    int mx = mousePressed->position.x;
                    int my = mousePressed->position.y;

                    // Ignore if clicked on text area or outside
                    if (my >= N * CELL) continue;

                    int gx = my / CELL;
                    int gy = mx / CELL;
                    if (!inBounds(gx, gy)) continue;

                    if (hStage == 0) {
                        // --- MOVE SELECTION ---
                        auto steps = getLegalStepMoves(game);
                        bool ok = false;
                        for (auto p : steps) {
                            if (p.first == gx && p.second == gy) {
                                ok = true;
                                break;
                            }
                        }
                        if (ok) {
                            applyStepMove(game, gx, gy);
                            hStage = 1; // Now switch to barrier placement stage
                        }
                    }
                    else if (hStage == 1) {
                        // --- BARRIER SELECTION ---
                        if (game.board[gx][gy] == EMPTY) {
                            placeBarrier(game, gx, gy);
                            game.isMaxTurn = true; // Turn switches to AI
                            hStage = 0;            // Reset stage for Human's next turn
                        }
                    }
                }
            }
        }

        // ==========================================================
        // 2. UPDATE: Game Logic and AI
        // ==========================================================

        // A) Is Game Over?
        if (hasNoMoves(game)) {
            // Draw final state and end
            window.clear();
            drawBoard(window, game);
            
            std::string msg;
            if (game.isMaxTurn) msg = "Game Over: HUMAN Won!";
            else                msg = "Game Over: AI Won!";
            
            std::cout << msg << std::endl;
            if (font.getInfo().family != "") {
                infoText.setString(msg);
                window.draw(infoText);
            }
            window.display();

            sf::sleep(sf::milliseconds(2000));
            window.close();
            break;
        }

        // B) AI Logic (FIXED PART HERE)
        if (window.isOpen() && game.isMaxTurn) {
            
            // 1. Inform user
            if (font.getInfo().family != "") {
                infoText.setString("AI is thinking...");
            }

            // 2. FORCE UPDATE SCREEN (To see the barrier)
            window.clear();
            drawBoard(window, game);     // Draw current board (with barrier)
            if (font.getInfo().family != "")
                window.draw(infoText);
            window.display();            // Push to screen

            // 3. LET OS BREATHE (To push drawing to screen)
            sf::sleep(sf::milliseconds(100));

            // 4. DO HEAVY CALCULATION
            Move ai = findBestMove(game, depthLimit);
            game = applyMove(game, ai);
        }

        // C) Human Turn Text Update
        if (!game.isMaxTurn && font.getInfo().family != "") {
            if (hStage == 0)
                infoText.setString("Your turn: Move your pawn.");
            else
                infoText.setString("Your turn: Place a barrier.");
        }

        // ==========================================================
        // 3. RENDER: Standard Drawing Loop
        // ==========================================================
        window.clear();
        drawBoard(window, game);
        if (font.getInfo().family != "")
            window.draw(infoText);
        window.display();
    }

    return 0;
}
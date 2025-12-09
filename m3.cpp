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

int turns = 1;       // Number of turns played

// FIX 1: Scores are set to massive values to ensure Heuristics never override them.
const int WIN_SCORE  = 1000000000;
const int LOSE_SCORE = -1000000000;

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
//  h(n) / eval function  ---> h = a_n + b_n + c_n + d_n
//==================================================

// a_n: Mobility Difference
int calculateMobility(const State& s) {
    int aiM = countMovesForPlayer(s, true);
    int huM = countMovesForPlayer(s, false);
    return (aiM - huM);
}

// b_n: Barrier Effect
int calculateBarriers(const State& s) {
    int blockedAroundAI = 0;
    int blockedAroundHU = 0;

    // --- Blocks around AI (Max) ---
    {
        State temp = s;
        temp.isMaxTurn = true;
        int ax, ay;
        getCurrentPlayerPos(temp, ax, ay);

        for (int k = 0; k < 8; k++) {
            int nx = ax + dx[k];
            int ny = ay + dy[k];
            if (inBounds(nx, ny) && s.board[nx][ny] == BLOCKED) {
                blockedAroundAI++;
            }
        }
    }

    // --- Blocks around Human (Min) ---
    {
        State temp = s;
        temp.isMaxTurn = false;
        int hx, hy;
        getCurrentPlayerPos(temp, hx, hy);

        for (int k = 0; k < 8; k++) {
            int nx = hx + dx[k];
            int ny = hy + dy[k];
            if (inBounds(nx, ny) && s.board[nx][ny] == BLOCKED) {
                blockedAroundHU++;
            }
        }
    }
    return (blockedAroundHU - blockedAroundAI);
}

// c_n: Voronoi Territory
void bfsDistances(const State& s, int startX, int startY, int distMap[N][N]) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            distMap[i][j] = 999;

    std::vector<std::pair<int,int>> q;
    q.push_back({startX, startY});
    distMap[startX][startY] = 0;

    size_t head = 0;
    while (head < q.size()) {
        auto [cx, cy] = q[head++];
        int currentDist = distMap[cx][cy];

        for (int k = 0; k < 8; ++k) {
            int nx = cx + dx[k];
            int ny = cy + dy[k];

            if (!inBounds(nx, ny)) continue;
            if (s.board[nx][ny] == BLOCKED) continue;

            if (distMap[nx][ny] > currentDist + 1) {
                distMap[nx][ny] = currentDist + 1;
                q.push_back({nx, ny});
            }
        }
    }
}

int calculateVoronoi(const State& s) {
    int distAI[N][N];
    int distHU[N][N];

    bfsDistances(s, s.aiX, s.aiY, distAI);
    bfsDistances(s, s.huX, s.huY, distHU);

    int score = 0;

    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (s.board[i][j] == BLOCKED) continue;
            if (distAI[i][j] == 999 && distHU[i][j] == 999) continue;

            if (distAI[i][j] < distHU[i][j]) {
                score++;
            } else if (distHU[i][j] < distAI[i][j]) {
                score--;
            }
        }
    }
    return score;
}

// d_n: Positional score
int calculatePositional(const State& s) {
    int mid = (N - 1) / 2;
    int aiDist = std::abs(s.aiX - mid) + std::abs(s.aiY - mid);
    int huDist = std::abs(s.huX - mid) + std::abs(s.huY - mid);

    int centerWeight = 3;
    int centerScore = centerWeight * (huDist - aiDist);

    auto edgePenalty = [&](int x, int y) {
        if (x == 0 || x == N - 1 || y == 0 || y == N - 1) return 1;
        return 0;
    };

    int aiEdge = edgePenalty(s.aiX, s.aiY);
    int huEdge = edgePenalty(s.huX, s.huY);
    int edgeWeight = 4;
    int edgeScore = edgeWeight * (huEdge - aiEdge);

    return centerScore + edgeScore;
}

// e_n: Local space
int countLocalSpaceAround(const State& s, int sx, int sy, int maxDist) {
    bool visited[N][N] = { false };
    struct Node { int x, y, dist; };
    std::vector<Node> q;
    q.push_back({sx, sy, 0});
    visited[sx][sy] = true;
    int count = 0;

    for (size_t i = 0; i < q.size(); ++i) {
        auto [x, y, dist] = q[i];
        if (!(x == sx && y == sy)) count++;
        if (dist == maxDist) continue;

        for (int k = 0; k < 8; ++k) {
            int nx = x + dx[k];
            int ny = y + dy[k];
            if (!inBounds(nx, ny)) continue;
            if (visited[nx][ny])  continue;
            if (s.board[nx][ny] == BLOCKED) continue;

            visited[nx][ny] = true;
            q.push_back({nx, ny, dist + 1});
        }
    }
    return count;
}

int calculateLocalSpace(const State& s) {
    int maxDist = 2;
    int aiSpace = countLocalSpaceAround(s, s.aiX, s.aiY, maxDist);
    int huSpace = countLocalSpaceAround(s, s.huX, s.huY, maxDist);
    return aiSpace - huSpace;
}

const double MAX_MOBILITY_DIFF    = 8.0;
const double MAX_BARRIER_DIFF     = 8.0;
const double MAX_VORONOI_DIFF     = 49.0;
const double MAX_POSITIONAL_ABS   = 22.0;
const double MAX_LOCAL_SPACE_DIFF = 24.0;

//==================================================
// FIX 2: Added 'depth' parameter to eval function
//==================================================
int eval(const State& s, int depth) {
    
    // 1) Terminal state: Win/Loss check
    if (hasNoMoves(s)) {
        if (s.isMaxTurn) {
            // AI cannot move -> LOST
            // Subtract depth to try and prolong the game (delaying the loss)
            return LOSE_SCORE - depth;
        } else {
            // Human cannot move -> AI WON
            // Add depth to prefer winning IMMEDIATELY.
            // The larger the depth (closer to current turn), the higher the score.
            return WIN_SCORE + depth;
        }
    }

    // 2) Heuristic calculations (If game continues)
    int blockedApprox = 2 * turns - 1;

    int a = calculateMobility(s);
    int b = calculateBarriers(s);
    int d = calculatePositional(s);

    auto clamp = [](double v) {
        if (v >  1.0) return  1.0;
        if (v < -1.0) return -1.0;
        return v;
    };

    double na = clamp(static_cast<double>(a) / MAX_MOBILITY_DIFF);
    double nb = clamp(static_cast<double>(b) / MAX_BARRIER_DIFF);
    double nd = clamp(static_cast<double>(d) / MAX_POSITIONAL_ABS);

    double score = 0.0;

    if (blockedApprox < 5) {
        score = 4.0 * na + 2.0 * nb + 3.0 * nd;
    } 
    else {
        int c = calculateVoronoi(s);
        int e = calculateLocalSpace(s);
        double nc = clamp(static_cast<double>(c) / MAX_VORONOI_DIFF);
        double ne = clamp(static_cast<double>(e) / MAX_LOCAL_SPACE_DIFF);

        score = 5.0 * na + 2.0 * nb + 7.0 * nc + 3.0 * nd + 10.0 * ne;
    }

    // FIX 3: Multiplier reduced (from 100,000 to 1,000)
    // This ensures the Heuristic score never reaches the WIN_SCORE.
    return static_cast<int>(score * 1000.0);
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
// FIX 4: Minimax now passes 'depth' to eval
//==================================================
int minimax(const State& s, int depth, int alpha, int beta) {
    if (depth == 0 || hasNoMoves(s)) {
        return eval(s, depth); // Passing depth parameter
    }

    auto moves = generateAllMoves(s);
    if (moves.empty()) return eval(s, depth);

    if (s.isMaxTurn) {
        int best = -2000000000; // Start lower than LOSE_SCORE

        for (const auto& m : moves) {
            State child = applyMove(s, m);
            int val = minimax(child, depth - 1, alpha, beta);

            best = max(best, val);
            alpha = max(alpha, val);
            if (beta <= alpha) break;
        }
        return best;
    } 
    else {
        int best = 2000000000; // Start higher than WIN_SCORE

        for (const auto& m : moves) {
            State child = applyMove(s, m);
            int val = minimax(child, depth - 1, alpha, beta);

            best = min(best, val);
            beta = min(beta, val);
            if (beta <= alpha) break;
        }
        return best;
    }
}

Move findBestMove(const State& s, int depth) {
    auto moves = generateAllMoves(s);
    Move best{};
    int bestVal = -2000000000; // Start with a very low value

    int alpha = -2000000000;
    int beta  =  2000000000;

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
// Initialize Game
//==================================================
void initializeGame(State& s) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            s.board[i][j] = EMPTY;

    s.aiX = 0;   s.aiY = 3;
    s.huX = 6;   s.huY = 3;

    s.board[s.aiX][s.aiY] = AI_PAWN;
    s.board[s.huX][s.huY] = HU_PAWN;

    s.isMaxTurn = false;
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

    sf::Font font;
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
    int hStage = 0;

    while (window.isOpen()) {
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }

            if (!game.isMaxTurn) {
                if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                    int mx = mousePressed->position.x;
                    int my = mousePressed->position.y;

                    if (my >= N * CELL) continue;

                    int gx = my / CELL;
                    int gy = mx / CELL;
                    if (!inBounds(gx, gy)) continue;

                    if (hStage == 0) {
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
                            hStage = 1; 
                        }
                    }
                    else if (hStage == 1) {
                        if (game.board[gx][gy] == EMPTY) {
                            placeBarrier(game, gx, gy);
                            game.isMaxTurn = true;
                            hStage = 0;            
                        }
                    }
                }
            }
        }

        if (hasNoMoves(game)) {
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

        if (window.isOpen() && game.isMaxTurn) {
            if (font.getInfo().family != "") {
                infoText.setString("AI is thinking...");
            }
            window.clear();
            drawBoard(window, game);
            if (font.getInfo().family != "")
                window.draw(infoText);
            window.display();
            sf::sleep(sf::milliseconds(100));

            Move ai = findBestMove(game, depthLimit);
            game = applyMove(game, ai);
            turns++;
            cout << "Turns: " << turns << endl;
        }

        if (!game.isMaxTurn && font.getInfo().family != "") {
            if (hStage == 0)
                infoText.setString("Your turn: Move your pawn.");
            else
                infoText.setString("Your turn: Place a barrier.");
        }

        window.clear();
        drawBoard(window, game);
        if (font.getInfo().family != "")
            window.draw(infoText);
        window.display();
    }

    return 0;
}
#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <optional>
#include <string>
#include <fstream> 
#include <algorithm>

using namespace std;

//==================================================
// GAME CONSTANTS
//==================================================
const int N    = 7;
const int CELL = 80;        
const int UI_HEIGHT = 40;   
const int DEPTH_LIMIT = 3;  

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
// LOGGING STRUCTURES (JSON Export için)
//==================================================
struct LogNode {
    int id;
    int parentId;
    int score;
    int depth;
    string type; // "MAX" or "MIN"
    string info; // "Leaf", "Pruned" etc.
};

struct LogTurn {
    int turnNumber;
    vector<LogNode> nodes; // Bu turda oluşan ağaç
    int bestMoveScore;
    int boardState[N][N];  // O anki tahta
};

// Global Log Verisi
vector<LogTurn> gameLog;
vector<LogNode> currentTurnNodes; 

//==================================================
// STATE STRUCTURE
//==================================================
struct State {
    int board[N][N];
    int aiX, aiY;
    int huX, huY;
    bool isMaxTurn;  
};

struct Move {
    int moveX, moveY;
    int removeX, removeY;
};

//==================================================
// FILE SAVING FUNCTION (game_data.js)
//==================================================
void saveGameLog() {
    ofstream file("game_data.js");
    if (!file.is_open()) {
        cout << "Hata: Dosya oluşturulamadı!" << endl;
        return;
    }

    file << "const GAME_DATA = [\n";

    for (size_t i = 0; i < gameLog.size(); i++) {
        const auto& turn = gameLog[i];
        file << "  {\n";
        file << "    \"turn\": " << turn.turnNumber << ",\n";
        file << "    \"bestScore\": " << turn.bestMoveScore << ",\n";
        
        // Board Array
        file << "    \"board\": [\n";
        for(int r=0; r<N; r++) {
            file << "      [";
            for(int c=0; c<N; c++) {
                file << turn.boardState[r][c];
                if(c < N-1) file << ",";
            }
            file << "]";
            if(r < N-1) file << ",";
            file << "\n";
        }
        file << "    ],\n";

        // Tree Nodes
        file << "    \"nodes\": [\n";
        for (size_t j = 0; j < turn.nodes.size(); j++) {
            const auto& node = turn.nodes[j];
            file << "      { "
                 << "\"id\": " << node.id << ", "
                 << "\"parent\": " << node.parentId << ", "
                 << "\"score\": " << node.score << ", "
                 << "\"depth\": " << node.depth << ", "
                 << "\"type\": \"" << node.type << "\", "
                 << "\"info\": \"" << node.info << "\""
                 << " }";
            if (j < turn.nodes.size() - 1) file << ",";
            file << "\n";
        }
        file << "    ]\n";
        file << "  }";
        if (i < gameLog.size() - 1) file << ",";
        file << "\n";
    }
    file << "];\n";
    file.close();
    cout << "BASARILI: Oyun verisi 'game_data.js' olarak kaydedildi." << endl;
}

//==================================================
// HELPER FUNCTIONS
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

bool placeBarrier(State& s, int x, int y) {
    if (!inBounds(x, y)) return false;
    if (s.board[x][y] != EMPTY) return false;
    s.board[x][y] = BLOCKED;
    return true;
}

void applyStepMove(State& s, int toX, int toY) {
    int px, py;
    getCurrentPlayerPos(s, px, py);
    int pawn = s.isMaxTurn ? AI_PAWN : HU_PAWN;
    s.board[px][py] = EMPTY;
    s.board[toX][toY] = pawn;
    if (s.isMaxTurn) { s.aiX = toX; s.aiY = toY; }
    else             { s.huX = toX; s.huY = toY; }
}

State applyMove(const State& s, const Move& m) {
    State ns = s;
    applyStepMove(ns, m.moveX, m.moveY);
    placeBarrier(ns, m.removeX, m.removeY);
    ns.isMaxTurn = !s.isMaxTurn;
    return ns;
}

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
// ADVANCED EVALUATION LOGIC
//==================================================

// a_n: Mobility
int calculateMobility(const State& s) {
    int aiM = countMovesForPlayer(s, true);
    int huM = countMovesForPlayer(s, false);
    return (aiM - huM);
}

// b_n: Barriers
int calculateBarriers(const State& s) {
    int blockedAroundAI = 0;
    int blockedAroundHU = 0;

    // AI surroundings
    {
        State temp = s; temp.isMaxTurn = true;
        int ax, ay; getCurrentPlayerPos(temp, ax, ay);
        for (int k = 0; k < 8; k++) {
            int nx = ax + dx[k]; int ny = ay + dy[k];
            if (inBounds(nx, ny) && s.board[nx][ny] == BLOCKED) blockedAroundAI++;
        }
    }
    // HU surroundings
    {
        State temp = s; temp.isMaxTurn = false;
        int hx, hy; getCurrentPlayerPos(temp, hx, hy);
        for (int k = 0; k < 8; k++) {
            int nx = hx + dx[k]; int ny = hy + dy[k];
            if (inBounds(nx, ny) && s.board[nx][ny] == BLOCKED) blockedAroundHU++;
        }
    }
    return (blockedAroundHU - blockedAroundAI);
}

// c_n: Reachable Area (BFS)
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
    if (hasNoMoves(s)) {
        return s.isMaxTurn ? LOSE_SCORE : WIN_SCORE;
    }

    int a = calculateMobility(s) * 5;     // weight = 5
    int b = calculateBarriers(s) * 2;     // weight = 2
    int c = calculateAreaControl(s) * 10; // weight = 10

    return a + b + c;
}

//==================================================
// MOVE GENERATION
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
// MINIMAX (LOGLAMA İLE)
//==================================================
int minimax(const State& s, int depth, int alpha, int beta, int parentId) {
    
    // 1. Düğümü oluştur ve kaydet
    int myId = (int)currentTurnNodes.size();
    currentTurnNodes.push_back({
        myId, parentId, 0, DEPTH_LIMIT - depth,
        s.isMaxTurn ? "MAX" : "MIN", ""
    });

    if (depth == 0 || hasNoMoves(s)) {
        int v = eval(s);
        currentTurnNodes[myId].score = v;
        currentTurnNodes[myId].info = "Leaf";
        return v;
    }

    auto moves = generateAllMoves(s);
    if (moves.empty()) {
        int v = eval(s);
        currentTurnNodes[myId].score = v;
        return v;
    }

    if (s.isMaxTurn) { // MAX
        int best = -1000000000;
        for (const auto& m : moves) {
            State child = applyMove(s, m);
            int val = minimax(child, depth - 1, alpha, beta, myId);

            best = max(best, val);
            alpha = max(alpha, val);

            if (beta <= alpha) {
                currentTurnNodes[myId].info = "Pruned (Beta)";
                break;
            }
        }
        currentTurnNodes[myId].score = best;
        return best;
    } 
    else { // MIN
        int best = 1000000000;
        for (const auto& m : moves) {
            State child = applyMove(s, m);
            int val = minimax(child, depth - 1, alpha, beta, myId);

            best = min(best, val);
            beta = min(beta, val);

            if (beta <= alpha) {
                currentTurnNodes[myId].info = "Pruned (Alpha)";
                break;
            }
        }
        currentTurnNodes[myId].score = best;
        return best;
    }
}

Move findBestMove(const State& s, int depth) {
    currentTurnNodes.clear();

    // ROOT NODE
    int rootId = 0;
    currentTurnNodes.push_back({ rootId, -1, 0, 0, "ROOT", "Start" });

    auto moves = generateAllMoves(s);
    Move best{};
    int bestVal = -1000000000;
    int alpha = -1000000000, beta = 1000000000;

    for (const auto& m : moves) {
        State child = applyMove(s, m);
        int val = minimax(child, depth - 1, alpha, beta, rootId);

        if (val > bestVal) {
            bestVal = val;
            best = m;
        }
        alpha = max(alpha, val);
    }
    
    if(!currentTurnNodes.empty()) currentTurnNodes[0].score = bestVal;

    // LOG KAYDI
    LogTurn turnLog;
    turnLog.turnNumber = (int)gameLog.size() + 1;
    turnLog.nodes = currentTurnNodes;
    turnLog.bestMoveScore = bestVal;
    
    // Board state kopyala
    for(int i=0; i<N; i++) {
        for(int j=0; j<N; j++) {
            turnLog.boardState[i][j] = s.board[i][j];
        }
    }
    
    gameLog.push_back(turnLog);

    return best;
}

//==================================================
// UI
//==================================================
void drawBoard(sf::RenderWindow& win, const State& s) {
    sf::RectangleShape cell(sf::Vector2f((float)CELL - 2, (float)CELL - 2));

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            int v = s.board[i][j];

            if (v == EMPTY) cell.setFillColor(sf::Color(180, 180, 180));
            else if (v == BLOCKED) cell.setFillColor(sf::Color::Black);
            else if (v == AI_PAWN) cell.setFillColor(sf::Color::Blue);
            else if (v == HU_PAWN) cell.setFillColor(sf::Color::Red);

            cell.setPosition(sf::Vector2f((float)(j * CELL + 2), (float)(i * CELL + 2)));
            win.draw(cell);
        }
    }
}

void initializeGame(State& s) {
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) s.board[i][j] = EMPTY;
    s.aiX = 0; s.aiY = 3; s.huX = 6; s.huY = 3;
    s.board[s.aiX][s.aiY] = AI_PAWN; s.board[s.huX][s.huY] = HU_PAWN;
    s.isMaxTurn = false; 
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
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.openFromFile("arial.ttf")) {
        if (!font.openFromFile("/Library/Fonts/Arial.ttf")) {
            // Font yoksa hata bas ama devam et
        }
    }

    sf::Text infoText(font, "", 20);
    infoText.setFillColor(sf::Color::White);
    infoText.setPosition(sf::Vector2f(5.f, (float)(N * CELL + 5)));
    if (font.getInfo().family != "") infoText.setString("Loglama Aktif. Oyunu oynayin.");

    State game;
    initializeGame(game);

    int depthLimit = DEPTH_LIMIT;
    int hStage = 0;

    while (window.isOpen()) {
        while (const std::optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                saveGameLog(); // Çıkışta kaydet
                window.close();
            }

            if (!game.isMaxTurn) {
                if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                    int my = mousePressed->position.y;
                    if (my < N * CELL) {
                        int gx = my / CELL;
                        int gy = mousePressed->position.x / CELL;
                        if (inBounds(gx, gy)) {
                            if (hStage == 0) {
                                auto steps = getLegalStepMoves(game);
                                for (auto p : steps) {
                                    if (p.first == gx && p.second == gy) {
                                        applyStepMove(game, gx, gy);
                                        hStage = 1;
                                        break;
                                    }
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
            }
        }

        if (hasNoMoves(game)) {
            // Game over
            window.clear();
            drawBoard(window, game);
            window.display();
            sf::sleep(sf::milliseconds(1000));
            
            saveGameLog();
            window.close();
            break;
        }

        if (window.isOpen() && game.isMaxTurn) {
            if (font.getInfo().family != "") infoText.setString("AI Dusunuyor...");
            window.clear(); drawBoard(window, game); 
            if (font.getInfo().family != "") window.draw(infoText); 
            window.display();
            
            sf::sleep(sf::milliseconds(100));

            Move ai = findBestMove(game, depthLimit);
            game = applyMove(game, ai);
            
            cout << "Turn " << gameLog.size() << " loglandi." << endl;
            if (font.getInfo().family != "") infoText.setString("AI Hamle Yapti.");
        }

        window.clear();
        drawBoard(window, game);
        
        if (!game.isMaxTurn && font.getInfo().family != "") {
             if (hStage == 0) infoText.setString("Sira Sende: Tasini Oynat");
             else infoText.setString("Sira Sende: Engel Koy");
        }
        
        if (font.getInfo().family != "") window.draw(infoText);
        window.display();
    }

    return 0;
}
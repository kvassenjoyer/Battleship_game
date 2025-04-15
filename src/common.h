#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <cstring>

#define MMF_NAME "/sea_battle_mmf"
#define SEM_CLIENT_READY "/sem_client_ready"
#define SEM_SERVER_READY "/sem_server_ready"
#define MMF_SIZE (sizeof(SharedMemory) + 1024)
#define MAX_PLAYERS 100
#define MAX_GAMES 20
#define STATS_FILE "player_stats.dat"
#define GAMES_FILE "games_data.dat"

// Размер игрового поля
#define BOARD_SIZE 10

// Состояния клетки игрового поля
enum CellState {
    EMPTY = 0,    // Пусто
    SHIP = 1,     // Корабль
    MISS = 2,     // Промах
    HIT = 3,      // Попадание
    DESTROYED = 4 // Уничтоженный корабль
};

// Типы кораблей
enum ShipType {
    BATTLESHIP = 4,   // Линкор (4 клетки)
    CRUISER = 3,      // Крейсер (3 клетки)
    DESTROYER = 2,    // Эсминец (2 клетки)
    SUBMARINE = 1     // Подводная лодка (1 клетка)
};

// Структура корабля
struct Ship {
    int x, y;         // Координаты начала
    int length;       // Длина
    bool horizontal;  // Ориентация (true - горизонтальная, false - вертикальная)
    int hits;         // Количество попаданий

    Ship() : x(-1), y(-1), length(0), horizontal(true), hits(0) {}

    bool isDestroyed() const {
        return hits >= length;
    }
};

// Количество кораблей каждого типа
#define BATTLESHIP_COUNT 1
#define CRUISER_COUNT 2
#define DESTROYER_COUNT 3
#define SUBMARINE_COUNT 4
#define TOTAL_SHIPS (BATTLESHIP_COUNT + CRUISER_COUNT + DESTROYER_COUNT + SUBMARINE_COUNT)

// Структура игрового поля
struct GameBoard {
    CellState cells[BOARD_SIZE][BOARD_SIZE];
    Ship ships[TOTAL_SHIPS];
    int shipsPlaced;  // Количество размещенных кораблей

    GameBoard() : shipsPlaced(0) {
        clear();
    }

    void clear() {
        memset(cells, EMPTY, sizeof(cells));
        shipsPlaced = 0;
        for (int i = 0; i < TOTAL_SHIPS; i++) {
            ships[i] = Ship();
        }
    }

    bool allShipsDestroyed() const {
        for (int i = 0; i < shipsPlaced; i++) {
            if (!ships[i].isDestroyed()) {
                return false;
            }
        }
        return shipsPlaced == TOTAL_SHIPS;
    }
};

// Состояния игры
enum GameState {
    WAITING_FOR_PLAYER = 0,   // Ожидание второго игрока
    PLACING_SHIPS = 1,        // Расстановка кораблей
    PLAYER1_TURN = 2,         // Ход первого игрока
    PLAYER2_TURN = 3,         // Ход второго игрока
    GAME_OVER = 4,             // Игра окончена
};

// Структура игры
struct Game {
    char name[64];                // Название игры
    char player1[64];             // Имя первого игрока
    char player2[64];             // Имя второго игрока
    GameBoard board1;             // Поле первого игрока
    GameBoard board2;             // Поле второго игрока
    GameState state;              // Состояние игры
    int winner;                   // Номер победителя (1 или 2), 0 - нет победителя
    bool active;                  // Активна ли игра

    Game() : state(WAITING_FOR_PLAYER), winner(0), active(false) {
        name[0] = '\0';
        player1[0] = '\0';
        player2[0] = '\0';
    }
};

// Структура данных игрока
struct PlayerStats {
    char username[64];
    int wins;
    int losses;
    bool active;
    bool inGame;
    char currentGame[64];

    PlayerStats() : wins(0), losses(0), active(false), inGame(false) {
        username[0] = '\0';
        currentGame[0] = '\0';
    }
};

// Структура сообщения
struct Message {
    enum Type {
        LOGIN = 3,
        LOGIN_RESPONSE = 4,
        CREATE_GAME = 5,
        CREATE_GAME_RESPONSE = 6,
        LIST_GAMES = 7,
        GAMES_LIST = 8,
        JOIN_GAME = 9,
        JOIN_GAME_RESPONSE = 10,
        PLACE_SHIP = 11,
        PLACE_SHIP_RESPONSE = 12,
        SHIPS_READY = 13,
        SHIPS_READY_RESPONSE = 14,
        MAKE_MOVE = 15,
        MOVE_RESULT = 16,
        GAME_STATUS = 17,
        GET_STATS = 18,
        STATS_DATA = 19,
        ERROR = 99
    };

    Type type;
    char username[64];
    char data[1024];
    bool newUser;  // Используется для LOGIN_RESPONSE, true = новый пользователь

    // Дополнительные поля для игры
    char gameName[64];
    int x, y;               // Координаты для хода или размещения корабля
    int shipLength;         // Длина корабля при размещении
    bool shipHorizontal;    // Ориентация корабля
    int hitResult;          // Результат хода (0 - промах, 1 - попадание, 2 - уничтожен корабль, 3 - победа)
    GameState gameState;    // Состояние игры
    char opponent[64];      // Имя оппонента
};

// Структура для общей памяти
struct SharedMemory {
    Message message;
    Game games[MAX_GAMES];
    int gameCount;
};

#endif // COMMON_H

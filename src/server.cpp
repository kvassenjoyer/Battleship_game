#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <fstream>
#include <cstring>
#include <vector>
#include <ctime>
#include <cstdlib>
#include "common.h"

// Global variables to store player data
PlayerStats g_players[MAX_PLAYERS];
int g_playerCount = 0;


// Глобальные переменные для обработки сигналов
SharedMemory* g_sharedMem = nullptr;
int g_shm_fd = -1;
sem_t* g_semClientReady = nullptr;
sem_t* g_semServerReady = nullptr;

// Загрузка статистики из файла
void loadStats() {
    std::ifstream file(STATS_FILE, std::ios::binary);
    if (!file) {
        std::cout << "Stats file not found, starting with empty database." << std::endl;
        g_playerCount = 0;
        return;
    }

    file.read(reinterpret_cast<char*>(&g_playerCount), sizeof(int));

    if (g_playerCount > MAX_PLAYERS) {
        std::cerr << "Warning: Corrupt stats file or too many players. Resetting." << std::endl;
        g_playerCount = 0;
        return;
    }

    for (int i = 0; i < g_playerCount; i++) {
        file.read(reinterpret_cast<char*>(&g_players[i]), sizeof(PlayerStats));
        g_players[i].active = false;
        g_players[i].inGame = false;
    }

    std::cout << "Loaded " << g_playerCount << " player records." << std::endl;
    file.close();
}

// Сохранение статистики в файл
void saveStats() {
    std::ofstream file(STATS_FILE, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open stats file for writing!" << std::endl;
        return;
    }

    file.write(reinterpret_cast<const char*>(&g_playerCount), sizeof(int));

    for (int i = 0; i < g_playerCount; i++) {
        file.write(reinterpret_cast<const char*>(&g_players[i]), sizeof(PlayerStats));
    }

    std::cout << "Saved " << g_playerCount << " player records." << std::endl;
    file.close();
}

// Поиск игрока по имени
int findPlayer(const char* username) {
    for (int i = 0; i < g_playerCount; i++) {
        if (strcmp(g_players[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

// Добавление нового игрока
int addPlayer(const char* username) {
    if (g_playerCount >= MAX_PLAYERS) {
        return -1; // max players reached
    }

    int idx = g_playerCount++;
    strncpy(g_players[idx].username, username, sizeof(g_players[idx].username) - 1);
    g_players[idx].username[sizeof(g_players[idx].username) - 1] = '\0';
    g_players[idx].wins = 0;
    g_players[idx].losses = 0;
    g_players[idx].active = true;
    g_players[idx].inGame = false;
    g_players[idx].currentGame[0] = '\0';

    return idx;
}

// Поиск игры по имени
int findGame(SharedMemory* sharedMem, const char* gameName) {
    for (int i = 0; i < sharedMem->gameCount; i++) {
        if (strcmp(sharedMem->games[i].name, gameName) == 0 && sharedMem->games[i].active) {
            return i;
        }
    }
    return -1;
}

// Создание новой игры
int createGame(SharedMemory* sharedMem, const char* gameName, const char* playerName) {
    if (sharedMem->gameCount >= MAX_GAMES) {
        return -1; // достигнут максимум игр
    }

    // Проверяем, не занято ли это имя
    if (findGame(sharedMem, gameName) != -1) {
        return -2; // игра с таким именем уже существует
        }

    int idx = sharedMem->gameCount++;
    strncpy(sharedMem->games[idx].name, gameName, sizeof(sharedMem->games[idx].name) - 1);
    sharedMem->games[idx].name[sizeof(sharedMem->games[idx].name) - 1] = '\0';

    strncpy(sharedMem->games[idx].player1, playerName, sizeof(sharedMem->games[idx].player1) - 1);
    sharedMem->games[idx].player1[sizeof(sharedMem->games[idx].player1) - 1] = '\0';

    sharedMem->games[idx].player2[0] = '\0';
    sharedMem->games[idx].state = WAITING_FOR_PLAYER;
    sharedMem->games[idx].winner = 0;
    sharedMem->games[idx].active = true;

    // Очищаем игровые поля
    sharedMem->games[idx].board1.clear();
    sharedMem->games[idx].board2.clear();

    // Обновляем статус игрока
    int playerIdx = findPlayer(playerName);
    if (playerIdx != -1) {
        g_players[playerIdx].inGame = true;
        strncpy(g_players[playerIdx].currentGame, gameName,
                sizeof(g_players[playerIdx].currentGame) - 1);
        g_players[playerIdx].currentGame[sizeof(g_players[playerIdx].currentGame) - 1] = '\0';
    }

    return idx;
}

// Подсоединение к игре
bool joinGame(SharedMemory* sharedMem, const char* gameName, const char* playerName) {
    int gameIdx = findGame(sharedMem, gameName);
    if (gameIdx == -1) {
        return false; // Игры не найдено
    }

    // Special case: создатель присоединяется в своей же игре
    if (strcmp(sharedMem->games[gameIdx].player1, playerName) == 0 &&
        sharedMem->games[gameIdx].state == PLACING_SHIPS) {
        return true; // Allow player1 to join their own game for ship placement
        }

    // Если игрка не в состоянии ожидания или игрок хочет подключится сам к себе - стоп
    if (sharedMem->games[gameIdx].state != WAITING_FOR_PLAYER) {
        return false;
        }

    // Подсоединяем игрока к игре
    strncpy(sharedMem->games[gameIdx].player2, playerName, sizeof(sharedMem->games[gameIdx].player2) - 1);
    sharedMem->games[gameIdx].player2[sizeof(sharedMem->games[gameIdx].player2) - 1] = '\0';

    // Состояние игры - расстановка корабле
    sharedMem->games[gameIdx].state = PLACING_SHIPS;

    // Обновляем статус игрока
    int playerIdx = findPlayer(playerName);
    if (playerIdx != -1) {
        g_players[playerIdx].inGame = true;
        strncpy(g_players[playerIdx].currentGame, gameName,
                sizeof(g_players[playerIdx].currentGame) - 1);
        g_players[playerIdx].currentGame[sizeof(g_players[playerIdx].currentGame) - 1] = '\0';
    }

    return true;
}


// Размещение корабля на поле
bool placeShip(GameBoard& board, int x, int y, int length, bool horizontal) {
    // Проверка выхода за границы поля
    if (x < 0 || y < 0 || x >= BOARD_SIZE || y >= BOARD_SIZE) {
        return false;
    }

    if (horizontal) {
        if (x + length > BOARD_SIZE) return false;
    } else {
        if (y + length > BOARD_SIZE) return false;
    }

    // Проверка пересечения с другими кораблями (включая соседние клетки)
    for (int i = -1; i <= length; i++) {
        for (int j = -1; j <= 1; j++) {
            int checkX = horizontal ? x + i : x + j;
            int checkY = horizontal ? y + j : y + i;

            if (checkX >= 0 && checkX < BOARD_SIZE && checkY >= 0 && checkY < BOARD_SIZE) {
                if (board.cells[checkY][checkX] == SHIP) {
                    return false;
                }
            }
        }
    }

    // Размещаем корабль на поле
    if (board.shipsPlaced >= TOTAL_SHIPS) {
        return false; // все корабли уже размещены
    }

    board.ships[board.shipsPlaced].x = x;
    board.ships[board.shipsPlaced].y = y;
    board.ships[board.shipsPlaced].length = length;
    board.ships[board.shipsPlaced].horizontal = horizontal;
    board.ships[board.shipsPlaced].hits = 0;

    // Отмечаем клетки на поле
    for (int i = 0; i < length; i++) {
        if (horizontal) {
            board.cells[y][x + i] = SHIP;
        } else {
            board.cells[y + i][x] = SHIP;
        }
    }

    board.shipsPlaced++;
    return true;
}

// Проверка, что все корабли размещены
bool areAllShipsPlaced(const GameBoard& board) {
    int expected[5] = {0, SUBMARINE_COUNT, DESTROYER_COUNT, CRUISER_COUNT, BATTLESHIP_COUNT};
    int actual[5] = {0}; // Индекс - длина корабля

    for (int i = 0; i < board.shipsPlaced; i++) {
        if (board.ships[i].length >= 1 && board.ships[i].length <= 4) {
            actual[board.ships[i].length]++;
        }
    }

    for (int i = 1; i <= 4; i++) {
        if (actual[i] != expected[i]) {
            return false;
        }
    }

    return true;
}


// Обработка хода игрока
int processMove(GameBoard& opponentBoard, int x, int y) {
    if (x < 0 || y < 0 || x >= BOARD_SIZE || y >= BOARD_SIZE) {
        return -1; // недопустимые координаты
    }

    // Уже стреляли в эту клетку
    if (opponentBoard.cells[y][x] == MISS || opponentBoard.cells[y][x] == HIT ||
        opponentBoard.cells[y][x] == DESTROYED) {
        return -2;
    }

    // Промах
    if (opponentBoard.cells[y][x] == EMPTY) {
        opponentBoard.cells[y][x] = MISS;
        return 0;
    }

    // Попадание
    if (opponentBoard.cells[y][x] == SHIP) {
        opponentBoard.cells[y][x] = HIT;

        // Проверяем, какой корабль поражен
        for (int i = 0; i < opponentBoard.shipsPlaced; i++) {
            Ship& ship = opponentBoard.ships[i];
            bool hit = false;

            for (int j = 0; j < ship.length; j++) {
                int shipX = ship.horizontal ? ship.x + j : ship.x;
                int shipY = ship.horizontal ? ship.y : ship.y + j;

                if (shipX == x && shipY == y) {
                    ship.hits++;
                    hit = true;
                    break;
                }
            }

            if (hit) {
                // Проверяем, уничтожен ли корабль
                if (ship.isDestroyed()) {
                    // Помечаем все клетки корабля как уничтоженные
                    for (int j = 0; j < ship.length; j++) {
                        int shipX = ship.horizontal ? ship.x + j : ship.x;
                        int shipY = ship.horizontal ? ship.y : ship.y + j;
                        opponentBoard.cells[shipY][shipX] = DESTROYED;
                    }

                    // Проверяем, все ли корабли уничтожены
                    if (opponentBoard.allShipsDestroyed()) {
                        return 3; // победа
                    }
                    return 2; // корабль уничтожен
                }
                return 1; // попадание
            }
        }
    }

    // Не должны сюда добраться, но на всякий случай
    return 0;
}

// Обработчик сигнала для корректного завершения
void signalHandler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\nReceived SIGINT. Saving data and cleaning up..." << std::endl;

        if (g_sharedMem) {
            saveStats(); // Updated to not use sharedMem
            munmap(g_sharedMem, MMF_SIZE);
        }

        // Rest of the handler remains the same
        if (g_semClientReady) sem_close(g_semClientReady);
        if (g_semServerReady) sem_close(g_semServerReady);

        sem_unlink(SEM_CLIENT_READY);
        sem_unlink(SEM_SERVER_READY);

        if (g_shm_fd != -1) close(g_shm_fd);
        shm_unlink(MMF_NAME);

        exit(0);
    }
}

// Расчет процента побед
float calculateWinRate(int wins, int losses) {
    int total = wins + losses;
    if (total == 0) {
        return 0.0f;
    }
    return (float)wins * 100.0f / (float)total;
}

// Функция для красивого вывода
void centerText(char* buffer, const char* text, size_t width) {
    size_t textLen = strlen(text);
    if (textLen >= width) {
        // If text is longer than width, just copy it
        strcpy(buffer, text);
    } else {
        // Calculate padding
        size_t padding = (width - textLen) / 2;
        // Use sprintf to center the text
        sprintf(buffer, "%*s%s%*s", (int)padding, "", text, (int)(width - textLen - padding), "");
    }
}

int main() {
    // Инициализируем генератор случайных чисел
    srand(static_cast<unsigned int>(time(nullptr)));

    // На всякий случай чистим
    shm_unlink(MMF_NAME);
    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_SERVER_READY);


    // Установка обработчика сигнала
    signal(SIGINT, signalHandler);
    std::cout << "Sigint handler initalized" << std::endl;

    std::cout << "Initializing shared memory..." << std::endl;
    // Создаем объект в разделяемой памяти
    g_shm_fd = shm_open(MMF_NAME, O_CREAT | O_RDWR, 0666);
    if (g_shm_fd == -1) {
        std::cerr << "Error creating shared memory: " << strerror(errno) << std::endl;
        return 1;
    }

    // Устанавливаем размер
    if (ftruncate(g_shm_fd, MMF_SIZE) == -1) {
        std::cerr << "Error setting shared memory size: " << strerror(errno) << std::endl;
        close(g_shm_fd);
        shm_unlink(MMF_NAME);
        return 1;
    }

    // Отображаем в память
    g_sharedMem = (SharedMemory*)mmap(NULL, MMF_SIZE,
                                  PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (g_sharedMem == MAP_FAILED) {
        std::cerr << "Error mapping shared memory: " << strerror(errno) << std::endl;
        close(g_shm_fd);
        shm_unlink(MMF_NAME);
        return 1;
    }

    // Ставим все в нули
    memset(&g_sharedMem->message, 0, sizeof(Message));
    g_sharedMem->gameCount = 0;

    // Безопасно инициализируем массивы
    for (int i = 0; i < MAX_PLAYERS; i++) {
        memset(&g_players[i], 0, sizeof(PlayerStats));
    }
    for (int i = 0; i < MAX_GAMES; i++) {
        memset(&g_sharedMem->games[i], 0, sizeof(Game));
    }
    std::cout << "Shared memory initalized" << std::endl;

    // Загружаем статистику и игры
    loadStats();
    // loadGames(g_sharedMem);
    std::cout << "Stats downloaded" << std::endl;

    std::cout << "Initializing semaphores..." << std::endl;
    // Создаем семафоры для синхронизации
    g_semClientReady = sem_open(SEM_CLIENT_READY, O_CREAT, 0666, 0);
    if (g_semClientReady == SEM_FAILED) {
        std::cerr << "Error creating client semaphore: " << strerror(errno) << std::endl;
        munmap(g_sharedMem, MMF_SIZE);
        close(g_shm_fd);
        shm_unlink(MMF_NAME);
        return 1;
    }

    g_semServerReady = sem_open(SEM_SERVER_READY, O_CREAT, 0666, 0);
    if (g_semServerReady == SEM_FAILED) {
        std::cerr << "Error creating server semaphore: " << strerror(errno) << std::endl;
        sem_close(g_semClientReady);
        sem_unlink(SEM_CLIENT_READY);
        munmap(g_sharedMem, MMF_SIZE);
        close(g_shm_fd);
        shm_unlink(MMF_NAME);
        return 1;
    }
    std::cout << "Initializing semaphores complete" << std::endl;

//    // Чистим все ожидающие сигналы на семафорах
//    while (sem_trywait(g_semClientReady) == 0) {
//        // Пустой цикл для очищения семафора
//    }
//
//    while (sem_trywait(g_semServerReady) == 0) {
//        // Пустой цикл для очищения семафора
//    }

    std::cout << "\nSea Battle Server started. Press Ctrl+C to save and exit." << std::endl;

    // Основной цикл сервера
    while (true) {
        // Ожидаем сообщение от клиента
        sem_wait(g_semClientReady);

        // Обрабатываем различные типы сообщений
        switch (g_sharedMem->message.type) {
            case Message::LOGIN:
                {
                    std::string username = g_sharedMem->message.username;
                    std::cout << "Login request from: " << username << std::endl;

                    int playerIdx = findPlayer(username.c_str());
                    bool isNewUser = (playerIdx == -1);
                    bool isAlreadyActive = (g_players[playerIdx].active == true);

                    if (isNewUser) {
                        playerIdx = addPlayer(username.c_str());
                        std::cout << "New player registered: " << username << std::endl;
                    } else {
                        g_players[playerIdx].active = true;
                        g_players[playerIdx].inGame = false; // Reset game status on login

                        std::cout << "Returning player: " << username
                                << " (W:" << g_players[playerIdx].wins
                                << "/L:" << g_players[playerIdx].losses << ")" << std::endl;
                    }

                    // Form response
                    g_sharedMem->message.type = Message::LOGIN_RESPONSE;
                    g_sharedMem->message.newUser = isNewUser;

                    if (isNewUser) {
                        strcpy(g_sharedMem->message.data, "Registration successful!");
                    } else if (isAlreadyActive) {
                        strcpy(g_sharedMem->message.data, "Already online");
                    } else {
                        sprintf(g_sharedMem->message.data,
                                "Welcome back, %s! Your stats: %d wins, %d losses",
                                username.c_str(),
                                g_players[playerIdx].wins,
                                g_players[playerIdx].losses);
                    }
                }
                break;

            case Message::CREATE_GAME:
                {
                    std::string gameName = g_sharedMem->message.data;
                    std::string username = g_sharedMem->message.username;

                    std::cout << "Create game request: " << gameName << " from " << username << std::endl;

                    int gameIdx = createGame(g_sharedMem, gameName.c_str(), username.c_str());
                    g_sharedMem->message.type = Message::CREATE_GAME_RESPONSE;

                    if (gameIdx == -1) {
                        strcpy(g_sharedMem->message.data, "Maximum number of games reached!");
                    } else if (gameIdx == -2) {
                        strcpy(g_sharedMem->message.data, "Game with this name already exists!");
                    } else {
                        sprintf(g_sharedMem->message.data,
                                "Game '%s' created successfully! Waiting for opponent...",
                                gameName.c_str());
                        g_sharedMem->message.gameState = WAITING_FOR_PLAYER;
                        strcpy(g_sharedMem->message.gameName, gameName.c_str());
                    }
                }
                break;

            case Message::LIST_GAMES:
                {
                    std::cout << "List games request from " << g_sharedMem->message.username << std::endl;

                    // Создаем список доступных игр
                    g_sharedMem->message.type = Message::GAMES_LIST;

                    std::string gamesList = "Available games:\n";
                    bool foundGames = false;

                    for (int i = 0; i < g_sharedMem->gameCount; i++) {
                        if (g_sharedMem->games[i].active) {
                            // Игры в статусе ожидания
                            if (g_sharedMem->games[i].state == WAITING_FOR_PLAYER &&
                                strcmp(g_sharedMem->games[i].player1, g_sharedMem->message.username) != 0) {
                                gamesList += "- ";
                                gamesList += g_sharedMem->games[i].name;
                                gamesList += " (created by ";
                                gamesList += g_sharedMem->games[i].player1;
                                gamesList += ")\n";
                                foundGames = true;
                                }
                        }
                    }

                    if (!foundGames) {
                        gamesList += "No games available. Create your own game!\n";
                    }

                    strncpy(g_sharedMem->message.data, gamesList.c_str(), sizeof(g_sharedMem->message.data) - 1);
                    g_sharedMem->message.data[sizeof(g_sharedMem->message.data) - 1] = '\0';
                }
                break;

            case Message::JOIN_GAME:
                {
                    std::string gameName = g_sharedMem->message.gameName;
                    std::string username = g_sharedMem->message.username;

                    std::cout << "Join game request: " << gameName << " from " << username << std::endl;

                    bool joined = joinGame(g_sharedMem, gameName.c_str(), username.c_str());
                    g_sharedMem->message.type = Message::JOIN_GAME_RESPONSE;

                    if (!joined) {
                        strcpy(g_sharedMem->message.data,
                               "Could not join game. It may not exist, already started, or you created it.");
                        g_sharedMem->message.gameState = GAME_OVER; // Для индикации клиенту об ошибке
                    } else {
                        sprintf(g_sharedMem->message.data,
                                "Successfully joined game '%s'! Place your ships.",
                                gameName.c_str());

                        // Находим игру для получения информации о состоянии
                        int gameIdx = findGame(g_sharedMem, gameName.c_str());
                        if (gameIdx != -1) {
                            g_sharedMem->message.gameState = g_sharedMem->games[gameIdx].state;
                            strcpy(g_sharedMem->message.gameName, gameName.c_str());

                            // Ставим нужного оппонент
                            if (strcmp(g_sharedMem->games[gameIdx].player1, username.c_str()) == 0) {
                                // Player 1 is joining, so opponent is player 2
                                strcpy(g_sharedMem->message.opponent, g_sharedMem->games[gameIdx].player2);
                            } else {
                                // Player 2 is joining, so opponent is player 1
                                strcpy(g_sharedMem->message.opponent, g_sharedMem->games[gameIdx].player1);
                            }
                        }
                    }
                }
            break;

            case Message::GAME_STATUS:
                {
                    std::string gameName = g_sharedMem->message.gameName;
                    std::string username = g_sharedMem->message.username;

                    // std::cout << "Game status request from " << username << " for game " << gameName << std::endl;

                    int gameIdx = findGame(g_sharedMem, gameName.c_str());
                    g_sharedMem->message.type = Message::GAME_STATUS;

                    if (gameIdx == -1) {
                        strcpy(g_sharedMem->message.data, "Game not found!");
                        g_sharedMem->message.gameState = GAME_OVER;
                        break;
                    }

                    // Возвращаем текущее состояние игры
                    g_sharedMem->message.gameState = g_sharedMem->games[gameIdx].state;

                    // Чей ход
                    bool isPlayer1 = (strcmp(g_sharedMem->games[gameIdx].player1, username.c_str()) == 0);
                    bool isPlayer2 = (strcmp(g_sharedMem->games[gameIdx].player2, username.c_str()) == 0);

                    if (!isPlayer1 && !isPlayer2) {
                        strcpy(g_sharedMem->message.data, "You are not a participant in this game!");
                        break;
                    }

                    // Для ждущего отправляем инфу о последнем ходе
                    if ((g_sharedMem->games[gameIdx].state == PLAYER1_TURN && isPlayer2) ||
                        (g_sharedMem->games[gameIdx].state == PLAYER2_TURN && isPlayer1)) {

                        // In a real implementation, we would store and retrieve the last move's coordinates and result
                        // For now, we'll use defaults
                        g_sharedMem->message.x = -1;
                        g_sharedMem->message.y = -1;
                        g_sharedMem->message.hitResult = -1;
                        strcpy(g_sharedMem->message.data, "Waiting for opponent's move");
                        } else {
                            sprintf(g_sharedMem->message.data, "It's your turn in game %s", gameName.c_str());
                        }
                }
                break;

            case Message::PLACE_SHIP:
                {
                    std::string gameName = g_sharedMem->message.gameName;
                    std::string username = g_sharedMem->message.username;
                    int x = g_sharedMem->message.x;
                    int y = g_sharedMem->message.y;
                    int length = g_sharedMem->message.shipLength;
                    bool horizontal = g_sharedMem->message.shipHorizontal;

                    std::cout << "Place ship request from " << username << " in game " << gameName
                              << " at (" << x << "," << y << "), length " << length
                              << (horizontal ? " horizontal" : " vertical") << std::endl;

                    int gameIdx = findGame(g_sharedMem, gameName.c_str());
                    g_sharedMem->message.type = Message::PLACE_SHIP_RESPONSE;

                    if (gameIdx == -1) {
                        strcpy(g_sharedMem->message.data, "Game not found!");
                        break;
                    }

                    // Определяем номер игрока
                    bool isPlayer1 = (strcmp(g_sharedMem->games[gameIdx].player1, username.c_str()) == 0);
                    bool isPlayer2 = (strcmp(g_sharedMem->games[gameIdx].player2, username.c_str()) == 0);

                    if (!isPlayer1 && !isPlayer2) {
                        strcpy(g_sharedMem->message.data, "You are not a participant in this game!");
                        break;
                    }

                    // Проверяем, что игра в фазе расстановки кораблей
                    if (g_sharedMem->games[gameIdx].state != PLACING_SHIPS) {
                        strcpy(g_sharedMem->message.data, "Game is not in the ship placement phase!");
                        break;
                    }

                    // Выбираем соответствующую доску
                    GameBoard& board = isPlayer1 ? g_sharedMem->games[gameIdx].board1 : g_sharedMem->games[gameIdx].board2;

                    // Проверяем, что осталось место для корабля
                    int shipsOfLength[5] = {0}; // Индекс - длина корабля
                    for (int i = 0; i < board.shipsPlaced; i++) {
                        shipsOfLength[board.ships[i].length]++;
                    }

                    bool canPlaceShip = false;
                    if (length == BATTLESHIP && shipsOfLength[BATTLESHIP] < BATTLESHIP_COUNT) {
                        canPlaceShip = true;
                    } else if (length == CRUISER && shipsOfLength[CRUISER] < CRUISER_COUNT) {
                        canPlaceShip = true;
                    } else if (length == DESTROYER && shipsOfLength[DESTROYER] < DESTROYER_COUNT) {
                        canPlaceShip = true;
                    } else if (length == SUBMARINE && shipsOfLength[SUBMARINE] < SUBMARINE_COUNT) {
                        canPlaceShip = true;
                    }

                    if (!canPlaceShip) {
                        strcpy(g_sharedMem->message.data, "You have placed all ships of this type!");
                        break;
                    }

                    // Размещаем корабль
                    bool placed = placeShip(board, x, y, length, horizontal);

                    if (!placed) {
                        strcpy(g_sharedMem->message.data, "Cannot place ship at this position!");
                    } else {
                        sprintf(g_sharedMem->message.data, "Ship of length %d placed successfully!", length);

                        // Проверяем, все ли корабли размещены
                        if (areAllShipsPlaced(board)) {
                            strcat(g_sharedMem->message.data, " All ships are now placed!");
                        }
                    }

                    // Отправляем обновленное количество размещенных кораблей
                    g_sharedMem->message.shipLength = board.shipsPlaced;
                }
                break;

            case Message::SHIPS_READY:
            {
                std::string gameName = g_sharedMem->message.gameName;
                std::string username = g_sharedMem->message.username;

                std::cout << "Ships ready notification from " << username << " in game " << gameName << std::endl;

                int gameIdx = findGame(g_sharedMem, gameName.c_str());
                g_sharedMem->message.type = Message::SHIPS_READY_RESPONSE;

                if (gameIdx == -1) {
                    strcpy(g_sharedMem->message.data, "Game not found!");
                    break;
                }

                // Определяем номер игрока
                bool isPlayer1 = (strcmp(g_sharedMem->games[gameIdx].player1, username.c_str()) == 0);
                bool isPlayer2 = (strcmp(g_sharedMem->games[gameIdx].player2, username.c_str()) == 0);

                if (!isPlayer1 && !isPlayer2) {
                    strcpy(g_sharedMem->message.data, "You are not a participant in this game!");
                    break;
                }

                // Проверяем, что игра в фазе расстановки кораблей
                if (g_sharedMem->games[gameIdx].state != PLACING_SHIPS) {
                    strcpy(g_sharedMem->message.data, "Game is not in the ship placement phase!");
                    break;
                }

                // Проверяем, все ли корабли размещены
                GameBoard& board = isPlayer1 ? g_sharedMem->games[gameIdx].board1 : g_sharedMem->games[gameIdx].board2;

                if (!areAllShipsPlaced(board)) {
                    strcpy(g_sharedMem->message.data, "You haven't placed all your ships yet!");
                    break;
                }

                // Проверяем, готовы ли оба игрока
                GameBoard& otherBoard = isPlayer1 ? g_sharedMem->games[gameIdx].board2 : g_sharedMem->games[gameIdx].board1;

                if (areAllShipsPlaced(otherBoard)) {
                    // Оба игрока готовы, начинаем игру
                    g_sharedMem->games[gameIdx].state = PLAYER1_TURN;
                    strcpy(g_sharedMem->message.data, "Both players are ready! Game starts now.");
                    g_sharedMem->message.gameState = PLAYER1_TURN;

                    // Указываем, чей сейчас ход
                    if (isPlayer1) {
                        strcat(g_sharedMem->message.data, " It's your turn!");
                        strcpy(g_sharedMem->message.opponent, g_sharedMem->games[gameIdx].player2);
                    } else {
                        strcat(g_sharedMem->message.data, " Waiting for opponent's move.");
                        strcpy(g_sharedMem->message.opponent, g_sharedMem->games[gameIdx].player1);
                    }
                } else {
                    // Ждем второго игрока
                    strcpy(g_sharedMem->message.data, "\nYour ships are ready! Waiting for your opponent...");
                    g_sharedMem->message.gameState = PLACING_SHIPS;

                    // Указываем оппонента
                    if (isPlayer1) {
                        strcpy(g_sharedMem->message.opponent, g_sharedMem->games[gameIdx].player2);
                    } else {
                        strcpy(g_sharedMem->message.opponent, g_sharedMem->games[gameIdx].player1);
                    }
                }
            }
            break;

            case Message::MAKE_MOVE:
            {
                std::string gameName = g_sharedMem->message.gameName;
                std::string username = g_sharedMem->message.username;
                int x = g_sharedMem->message.x;
                int y = g_sharedMem->message.y;

                std::cout << "Move request from " << username << " in game " << gameName
                          << " at (" << x << "," << y << ")" << std::endl;

                int gameIdx = findGame(g_sharedMem, gameName.c_str());
                g_sharedMem->message.type = Message::MOVE_RESULT;

                if (gameIdx == -1) {
                    strcpy(g_sharedMem->message.data, "Game not found!");
                    break;
                }

                // Определяем номер игрока
                bool isPlayer1 = (strcmp(g_sharedMem->games[gameIdx].player1, username.c_str()) == 0);
                bool isPlayer2 = (strcmp(g_sharedMem->games[gameIdx].player2, username.c_str()) == 0);

                if (!isPlayer1 && !isPlayer2) {
                    strcpy(g_sharedMem->message.data, "You are not a participant in this game!");
                    break;
                }

                // Проверяем, чей сейчас ход
                if ((g_sharedMem->games[gameIdx].state == PLAYER1_TURN && !isPlayer1) ||
                    (g_sharedMem->games[gameIdx].state == PLAYER2_TURN && !isPlayer2)) {
                    strcpy(g_sharedMem->message.data, "It's not your turn!");
                    break;
                }

                // Выполняем ход
                GameBoard& targetBoard = isPlayer1 ? g_sharedMem->games[gameIdx].board2 : g_sharedMem->games[gameIdx].board1;
                int result = processMove(targetBoard, x, y);

                if (result == -1) {
                    strcpy(g_sharedMem->message.data, "Invalid coordinates!");
                    break;
                } else if (result == -2) {
                    strcpy(g_sharedMem->message.data, "You already fired at this position!");
                    break;
                }

                // Обрабатываем результат хода
                g_sharedMem->message.hitResult = result;

                    if (result == 0) {
                        centerText(g_sharedMem->message.data, "❌ Miss! ❌", 54);
                        // Переход хода к другому игроку
                        g_sharedMem->games[gameIdx].state = isPlayer1 ? PLAYER2_TURN : PLAYER1_TURN;
                        g_sharedMem->message.gameState = g_sharedMem->games[gameIdx].state;
                    } else if (result == 1) {
                        centerText(g_sharedMem->message.data, "💥 Hit! 💥", 54);
                        // Игрок продолжает ход после попадания
                        g_sharedMem->message.gameState = g_sharedMem->games[gameIdx].state;
                    } else if (result == 2) {
                        centerText(g_sharedMem->message.data, "🔥 Ship destroyed! 🔥", 54);
                        // Игрок продолжает ход после уничтожения корабля
                        g_sharedMem->message.gameState = g_sharedMem->games[gameIdx].state;
                    } else if (result == 3) {
                        // Победа - все корабли уничтожены
                        centerText(g_sharedMem->message.data, "🌟 Victory! All enemy ships destroyed! 🌟", 30);
                        g_sharedMem->games[gameIdx].state = GAME_OVER;
                        g_sharedMem->games[gameIdx].winner = isPlayer1 ? 1 : 2;
                        g_sharedMem->message.gameState = GAME_OVER;

                    // Обновляем статистику игроков
                    int winnerIdx = findPlayer(username.c_str());
                    int loserIdx = findPlayer(isPlayer1 ? g_sharedMem->games[gameIdx].player2 : g_sharedMem->games[gameIdx].player1);

                    if (winnerIdx != -1) {
                        g_players[winnerIdx].wins++;
                        g_players[winnerIdx].inGame = false;
                        g_players[winnerIdx].currentGame[0] = '\0';
                    }

                    if (loserIdx != -1) {
                        g_players[loserIdx].losses++;
                        g_players[loserIdx].inGame = false;
                        g_players[loserIdx].currentGame[0] = '\0';
                    }
                }
            }
            break;

            case Message::GET_STATS:
                {
                    std::string username = g_sharedMem->message.username;
                    std::cout << "Stats request from " << username << std::endl;

                    int playerIdx = findPlayer(username.c_str());
                    g_sharedMem->message.type = Message::STATS_DATA;

                    if (playerIdx == -1) {
                        strcpy(g_sharedMem->message.data, "Player not found!");
                    } else {
                        sprintf(g_sharedMem->message.data,
                                "Statistics for %s:\nWins: %d\nLosses: %d\nWin rate: %.1f%%",
                                username.c_str(),
                                g_players[playerIdx].wins,
                                g_players[playerIdx].losses,
                                calculateWinRate(g_players[playerIdx].wins, g_players[playerIdx].losses));
                    }
                }
                break;

            default:
                std::cout << "Received unknown message type: " << g_sharedMem->message.type << std::endl;
                g_sharedMem->message.type = Message::ERROR;
                strcpy(g_sharedMem->message.data, "Unknown command");
                break;
        }

        // Уведомляем клиента, что ответ готов
        sem_post(g_semServerReady);
    }

    saveStats();
    // saveGames(g_sharedMem);
    munmap(g_sharedMem, MMF_SIZE);
    sem_close(g_semClientReady);
    sem_close(g_semServerReady);
    sem_unlink(SEM_CLIENT_READY);
    sem_unlink(SEM_SERVER_READY);
    close(g_shm_fd);
    shm_unlink(MMF_NAME);

    return 0;
}
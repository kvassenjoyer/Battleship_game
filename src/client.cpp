#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include "common.h"

// Отображение игрового поля
void displayBoard(const CellState board[BOARD_SIZE][BOARD_SIZE], bool hideShips = false) {
    std::cout << "  ";
    for (int x = 0; x < BOARD_SIZE; x++) {
        std::cout << " " << x;
    }
    std::cout << std::endl;

    for (int y = 0; y < BOARD_SIZE; y++) {
        std::cout << y << " ";
        for (int x = 0; x < BOARD_SIZE; x++) {
            char symbol;
            switch (board[y][x]) {
                case EMPTY:
                    symbol = '.';
                    break;
                case SHIP:
                    symbol = hideShips ? '.' : 'S';
                    break;
                case MISS:
                    symbol = 'o';
                    break;
                case HIT:
                    symbol = 'X';
                    break;
                case DESTROYED:
                    symbol = '#';
                    break;
                default:
                    symbol = '?';
            }
            std::cout << " " << symbol;
        }
        std::cout << std::endl;
    }
}

// Function to display boards horizontally (side by side)
void displayBoardsHorizontally(const CellState myBoard[BOARD_SIZE][BOARD_SIZE],
                             const CellState enemyBoard[BOARD_SIZE][BOARD_SIZE],
                             bool hideEnemyShips = true) {
    // Header
    std::cout << "      Your Board                Enemy Board      " << std::endl;

    // Column numbers
    std::cout << "  ";
    for (int x = 0; x < BOARD_SIZE; x++) {
        std::cout << " " << x;
    }
    std::cout << "      ";
    for (int x = 0; x < BOARD_SIZE; x++) {
        std::cout << " " << x;
    }
    std::cout << std::endl;

    // Board contents
    for (int y = 0; y < BOARD_SIZE; y++) {
        // First board row
        std::cout << y << " ";
        for (int x = 0; x < BOARD_SIZE; x++) {
            char symbol;
            switch (myBoard[y][x]) {
            case EMPTY: symbol = '.'; break;
            case SHIP: symbol = 'S'; break;
            case MISS: symbol = 'o'; break;
            case HIT: symbol = 'X'; break;
            case DESTROYED: symbol = '#'; break;
            default: symbol = '?';
            }
            std::cout << " " << symbol;
        }

        // Spacing between boards
        std::cout << "    ";

        // Second board row
        std::cout << y << " ";
        for (int x = 0; x < BOARD_SIZE; x++) {
            char symbol;
            switch (enemyBoard[y][x]) {
            case EMPTY: symbol = '.'; break;
            case SHIP: symbol = hideEnemyShips ? '.' : 'S'; break;
            case MISS: symbol = 'o'; break;
            case HIT: symbol = 'X'; break;
            case DESTROYED: symbol = '#'; break;
            default: symbol = '?';
            }
            std::cout << " " << symbol;
        }
        std::cout << std::endl;
    }
}

bool waitForOpponentShips(SharedMemory* sharedMem, sem_t* semClientReady, sem_t* semServerReady,
                         std::string username, std::string gameName) {
    std::cout << "\nWaiting for your opponent to place their ships..." << std::endl;

    int pollCount = 0;
    const int MAX_POLLS = 300; // Ждем 5 минут

    while (pollCount < MAX_POLLS) {
        // Poll for game status
        sharedMem->message.type = Message::GAME_STATUS;
        strcpy(sharedMem->message.username, username.c_str());
        strcpy(sharedMem->message.gameName, gameName.c_str());

        sem_post(semClientReady);
        sem_wait(semServerReady);

        if (sharedMem->message.type == Message::GAME_STATUS) {
            // Игра началась? (все поставили корабли)
            if (sharedMem->message.gameState == PLAYER1_TURN ||
                sharedMem->message.gameState == PLAYER2_TURN) {
                std::cout << "\nYour opponent has finished placing ships!" << std::endl;
                std::cout << "Game is starting now..." << std::endl;
                return true;
                }

            // Check if the game has ended unexpectedly
            if (sharedMem->message.gameState == GAME_OVER) {
                std::cout << "\nGame has ended: " << sharedMem->message.data << std::endl;
                return false;
            }
        }

        // Мини анимашка ожидания
        if (pollCount % 5 == 0) {
            std::cout << "." << std::flush;
        }

        sleep(1); // Ждем секунду перед проверкой на соединение
        pollCount++;
    }

    std::cout << "\nWaited too long for opponent. You can check back later." << std::endl;
    return false;
}

// Функция для размещения кораблей
void placeShips(SharedMemory* sharedMem, sem_t* semClientReady, sem_t* semServerReady,
                std::string username, std::string gameName) {
    system("clear");
    std::cout << "\n====== Ship Placement ======\n" << std::endl;
    std::cout << "You need to place:\n";
    std::cout << "- " << BATTLESHIP_COUNT << " battleships (4 cells)\n";
    std::cout << "- " << CRUISER_COUNT << " cruisers (3 cells)\n";
    std::cout << "- " << DESTROYER_COUNT << " destroyers (2 cells)\n";
    std::cout << "- " << SUBMARINE_COUNT << " submarines (1 cell)\n";

    // Локальная копия доски для отображения
    CellState localBoard[BOARD_SIZE][BOARD_SIZE] = {};
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            localBoard[y][x] = EMPTY;
        }
    }

    // Массив для отслеживания размещенных кораблей
    int shipsPlaced[5] = {0}; // 0 не используется, 1-4 - длины кораблей

    // Цикл размещения кораблей
    while (true) {
        std::cout << "\nCurrent board:" << std::endl;
        displayBoard(localBoard);

        std::cout << "\nRemaining ships:" << std::endl;
        std::cout << "- Battleships (4): " << BATTLESHIP_COUNT - shipsPlaced[4] << std::endl;
        std::cout << "- Cruisers (3): " << CRUISER_COUNT - shipsPlaced[3] << std::endl;
        std::cout << "- Destroyers (2): " << DESTROYER_COUNT - shipsPlaced[2] << std::endl;
        std::cout << "- Submarines (1): " << SUBMARINE_COUNT - shipsPlaced[1] << std::endl;

        // Проверяем, все ли корабли размещены
        if (shipsPlaced[1] == SUBMARINE_COUNT &&
            shipsPlaced[2] == DESTROYER_COUNT &&
            shipsPlaced[3] == CRUISER_COUNT &&
            shipsPlaced[4] == BATTLESHIP_COUNT) {

            // Отправляем серверу уведомление, что корабли готовы
            sharedMem->message.type = Message::SHIPS_READY;
            strcpy(sharedMem->message.username, username.c_str());
            strcpy(sharedMem->message.gameName, gameName.c_str());

            sem_post(semClientReady);
            sem_wait(semServerReady);

            if (sharedMem->message.type == Message::SHIPS_READY_RESPONSE) {
                std::cout << sharedMem->message.data << std::endl;
                break;
            } else {
                std::cerr << "Unexpected server response!" << std::endl;
                return;
            }
        }

        // Ввод данных для размещения корабля
        int shipLength;
        do {
            std::cout << "\nEnter ship length (1-4): ";
            std::string input;
            std::getline(std::cin, input);
            std::stringstream ss(input);
            if (!(ss >> shipLength) || shipLength < 1 || shipLength > 4) {
                std::cout << "Invalid length. Please enter a number between 1 and 4." << std::endl;
                shipLength = 0;
                continue;
            }

            // Проверяем, остались ли корабли этой длины
            if ((shipLength == 4 && shipsPlaced[4] >= BATTLESHIP_COUNT) ||
                (shipLength == 3 && shipsPlaced[3] >= CRUISER_COUNT) ||
                (shipLength == 2 && shipsPlaced[2] >= DESTROYER_COUNT) ||
                (shipLength == 1 && shipsPlaced[1] >= SUBMARINE_COUNT)) {
                std::cout << "You have already placed all ships of this length!" << std::endl;
                shipLength = 0;
            }
        } while (shipLength < 1 || shipLength > 4);

        // Получаем координаты
        int x, y;
        std::cout << "Enter coordinates (format: x y): ";
        std::string input;
        std::getline(std::cin, input);
        std::stringstream ss(input);
        if (!(ss >> x >> y) || x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            std::cout << "Invalid coordinates! Please try again." << std::endl;
            continue;
        }

        // Запрос ориентации (для кораблей длиннее 1)
        bool horizontal = true;
        if (shipLength > 1) {
            std::cout << "Orientation (h - horizontal, v - vertical): ";
            std::getline(std::cin, input);
            horizontal = (input != "v" && input != "V");
        }

        // Отправляем запрос на размещение корабля
        sharedMem->message.type = Message::PLACE_SHIP;
        strcpy(sharedMem->message.username, username.c_str());
        strcpy(sharedMem->message.gameName, gameName.c_str());
        sharedMem->message.x = x;
        sharedMem->message.y = y;
        sharedMem->message.shipLength = shipLength;
        sharedMem->message.shipHorizontal = horizontal;

        sem_post(semClientReady);
        sem_wait(semServerReady);

        if (sharedMem->message.type == Message::PLACE_SHIP_RESPONSE) {
            std::cout << sharedMem->message.data << std::endl;

            // Если корабль успешно размещен, обновляем локальную доску
            if (strstr(sharedMem->message.data, "successfully") != nullptr) {
                // Размещение на локальной доске
                for (int i = 0; i < shipLength; i++) {
                    int shipX = horizontal ? x + i : x;
                    int shipY = horizontal ? y : y + i;
                    localBoard[shipY][shipX] = SHIP;
                }

                // Обновляем счетчик размещенных кораблей
                shipsPlaced[shipLength]++;
            }

            system("clear");
        } else {
            std::cerr << "Unexpected server response!" << std::endl;
        }
    }
}

// Функция для игрового процесса
void playGame(SharedMemory* sharedMem, sem_t* semClientReady, sem_t* semServerReady,
             std::string username, std::string gameName, GameState initialState, std::string opponent) {
    system("clear");
    std::cout << "\n====== Game Started ======\n" << std::endl;
    std::cout << "You are playing against: " << opponent << std::endl;

    // Локальные копии досок для отображения
    CellState myBoard[BOARD_SIZE][BOARD_SIZE] = {};   // Моя доска
    CellState enemyBoard[BOARD_SIZE][BOARD_SIZE] = {}; // Доска противника

    // Инициализация пустыми клетками только для доски противника
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            enemyBoard[y][x] = EMPTY;
        }
    }

    // Запрашиваем состояние доски
    sharedMem->message.type = Message::GAME_STATUS;
    strcpy(sharedMem->message.username, username.c_str());
    strcpy(sharedMem->message.gameName, gameName.c_str());

    sem_post(semClientReady);
    sem_wait(semServerReady);

    int playerIdx = -1;
    // Находим игру и определяем какой мы игрок
    for (int i = 0; i < sharedMem->gameCount; i++) {
        if (strcmp(sharedMem->games[i].name, gameName.c_str()) == 0) {
            if (strcmp(sharedMem->games[i].player1, username.c_str()) == 0) {
                // Мы игрок 1, копируем доску 1
                for (int y = 0; y < BOARD_SIZE; y++) {
                    for (int x = 0; x < BOARD_SIZE; x++) {
                        myBoard[y][x] = sharedMem->games[i].board1.cells[y][x];
                    }
                }
                playerIdx = 1;
                break;
            } else if (strcmp(sharedMem->games[i].player2, username.c_str()) == 0) {
                // Мы игрок 2, копируем доску 2
                for (int y = 0; y < BOARD_SIZE; y++) {
                    for (int x = 0; x < BOARD_SIZE; x++) {
                        myBoard[y][x] = sharedMem->games[i].board2.cells[y][x];
                    }
                }
                playerIdx = 2;
                break;
            }
        }
    }

    bool isPlayer1 = (playerIdx == 1);

    // Текущее состояние игры
    GameState gameState = initialState;
    bool isMyTurn = (gameState == PLAYER1_TURN && isPlayer1) ||
                    (gameState == PLAYER2_TURN && !isPlayer1);

    while (gameState != GAME_OVER) {
//        // Отображаем обе доски
//        std::cout << "\nYour board:" << std::endl;
//        displayBoard(myBoard);
//
//        std::cout << "\nEnemy board:" << std::endl;
//        displayBoard(enemyBoard, true);  // Скрываем корабли противника
        std::cout << std::endl;
        displayBoardsHorizontally(myBoard, enemyBoard);

        if (isMyTurn) {
            std::cout << "\nYour turn! Enter coordinates to fire (format: x y): ";
            std::string input;
            std::getline(std::cin, input);
            system("clear");

            // Обработка выхода из игры
            if (input == "quit" || input == "exit") {
                std::cout << "Exiting game..." << std::endl;
                break;
            }

            std::stringstream ss(input);
            int x, y;
            if (!(ss >> x >> y) || x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
                std::cout << "Invalid coordinates! Please try again." << std::endl;
                continue;
            }

            // Отправляем ход на сервер
            sharedMem->message.type = Message::MAKE_MOVE;
            strcpy(sharedMem->message.username, username.c_str());
            strcpy(sharedMem->message.gameName, gameName.c_str());
            sharedMem->message.x = x;
            sharedMem->message.y = y;

            sem_post(semClientReady);
            sem_wait(semServerReady);

            if (sharedMem->message.type == Message::MOVE_RESULT) {
                std::cout << sharedMem->message.data << std::endl;

                // Обновляем локальную доску противника в соответствии с результатом
                if (sharedMem->message.hitResult >= 0) {
                    switch (sharedMem->message.hitResult) {
                        case 0: // Промах
                            enemyBoard[y][x] = MISS;
                            isMyTurn = false;
                            break;
                        case 1: // Попадание
                            enemyBoard[y][x] = HIT;
                            break;
                        case 2: // Корабль уничтожен
                            // Фулл обновляем доску для отметки всего корябля пореженным
                            for (int i = 0; i < sharedMem->gameCount; i++) {
                                if (strcmp(sharedMem->games[i].name, gameName.c_str()) == 0) {
                                    // Кто мы?
                                    const GameBoard& updatedBoard = isPlayer1 ? sharedMem->games[i].board2 : sharedMem->games[i].board1;

                                    // Берем только уничтоженные клетки
                                    for (int boardY = 0; boardY < BOARD_SIZE; boardY++) {
                                        for (int boardX = 0; boardX < BOARD_SIZE; boardX++) {
                                            if (updatedBoard.cells[boardY][boardX] == DESTROYED) {
                                                enemyBoard[boardY][boardX] = DESTROYED;
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                            break;
                        case 3: // Победа
                            enemyBoard[y][x] = DESTROYED;
                            gameState = GAME_OVER;
                            std::cout << "\nCongratulations! You won the game!" << std::endl;
                            break;
                    }
                }

                // Обновляем состояние игры
                gameState = sharedMem->message.gameState;
            } else {
                std::cerr << "Unexpected server response!" << std::endl;
            }
        } else {
            std::cout << "\nWaiting for opponent's move..." << std::endl;

            // Чекаем обновления игры пока ждем оппонента
            bool opponentMoved = false;
            while (!opponentMoved) {
                // Чекаем обновы
                sharedMem->message.type = Message::GAME_STATUS;
                strcpy(sharedMem->message.username, username.c_str());
                strcpy(sharedMem->message.gameName, gameName.c_str());

                sem_post(semClientReady);
                sem_wait(semServerReady);

                if (sharedMem->message.type == Message::GAME_STATUS) {
                    GameState updatedState = sharedMem->message.gameState;

                    // Нащ ход?
                    if ((updatedState == PLAYER1_TURN && isPlayer1) ||
                        (updatedState == PLAYER2_TURN && !isPlayer1)) {
                        isMyTurn = true;
                        opponentMoved = true;
                        gameState = updatedState;

                        // Обновляем доску на основе данных сервера
                        // Соединяем удары и нашу доску
                        for (int i = 0; i < sharedMem->gameCount; i++) {
                            if (strcmp(sharedMem->games[i].name, gameName.c_str()) == 0) {
                                if (isPlayer1) {
                                    // Мы игрок 1 - копируем доску 1, которая содержит удары противника
                                    for (int y = 0; y < BOARD_SIZE; y++) {
                                        for (int x = 0; x < BOARD_SIZE; x++) {
                                            myBoard[y][x] = sharedMem->games[i].board1.cells[y][x];
                                        }
                                    }
                                } else {
                                    // Мы игрок 2 - копируем доску 2, которая содержит удары противника
                                    for (int y = 0; y < BOARD_SIZE; y++) {
                                        for (int x = 0; x < BOARD_SIZE; x++) {
                                            myBoard[y][x] = sharedMem->games[i].board2.cells[y][x];
                                        }
                                    }
                                }
                                break;
                            }
                        }
                        system("clear");
                        std::cout << "     Your opponent made a move. Your turn now!" << std::endl;
                    } else if (updatedState == GAME_OVER) {
                        gameState = GAME_OVER;
                        opponentMoved = true;

                        // Check if we lost by updating our board one last time
                        for (int i = 0; i < sharedMem->gameCount; i++) {
                            if (strcmp(sharedMem->games[i].name, gameName.c_str()) == 0) {
                                if (isPlayer1) {
                                    for (int y = 0; y < BOARD_SIZE; y++) {
                                        for (int x = 0; x < BOARD_SIZE; x++) {
                                            myBoard[y][x] = sharedMem->games[i].board1.cells[y][x];
                                        }
                                    }
                                } else {
                                    for (int y = 0; y < BOARD_SIZE; y++) {
                                        for (int x = 0; x < BOARD_SIZE; x++) {
                                            myBoard[y][x] = sharedMem->games[i].board2.cells[y][x];
                                        }
                                    }
                                }
                                break;
                            }
                        }
                        system("clear");
                        std::cout << "😭 Game ended! Your opponent has won 😭" << std::endl;
                    }
                }

                if (!opponentMoved) {
                    sleep(1); // Ждем немного снова
                }
            }
        }
    }

    std::cout << "\nGame over!" << std::endl;
}

// Функция для получения и отображения статистики
void viewStats(SharedMemory* sharedMem, sem_t* semClientReady, sem_t* semServerReady, std::string username) {
    sharedMem->message.type = Message::GET_STATS;
    strcpy(sharedMem->message.username, username.c_str());

    sem_post(semClientReady);
    sem_wait(semServerReady);

    if (sharedMem->message.type == Message::STATS_DATA) {
        system("clear");
        std::cout << "\n====== Player Statistics ======\n" << std::endl;
        std::cout << sharedMem->message.data << std::endl;
    } else {
        std::cerr << "Error retrieving statistics!" << std::endl;
    }
}

// Функция для получения списка доступных игр
std::string getGamesList(SharedMemory* sharedMem, sem_t* semClientReady, sem_t* semServerReady, std::string username) {
    sharedMem->message.type = Message::LIST_GAMES;
    strcpy(sharedMem->message.username, username.c_str());

    sem_post(semClientReady);
    sem_wait(semServerReady);

    if (sharedMem->message.type == Message::GAMES_LIST) {
        return sharedMem->message.data;
    } else {
        return "Error retrieving games list!";
    }
}

int main() {
    // Открываем существующий объект памяти
    int fd = shm_open(MMF_NAME, O_RDWR, 0666);
    if (fd == -1) {
        std::cerr << "Error opening shared memory. Is the server running?" << std::endl;
        return 1;
    }

    // Отображаем память
    SharedMemory* sharedMem = (SharedMemory*)mmap(NULL, MMF_SIZE,
                                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sharedMem == MAP_FAILED) {
        std::cerr << "Error mapping shared memory: " << strerror(errno) << std::endl;
        close(fd);
        return 1;
    }

    // Открываем существующие семафоры
    sem_t* semClientReady = sem_open(SEM_CLIENT_READY, 0);
    sem_t* semServerReady = sem_open(SEM_SERVER_READY, 0);

    if (semClientReady == SEM_FAILED || semServerReady == SEM_FAILED) {
        std::cerr << "Error opening semaphores: " << strerror(errno) << std::endl;
        munmap(sharedMem, MMF_SIZE);
        close(fd);
        return 1;
    }

    std::cout << "====== Welcome to Sea Battle ======\n" << std::endl;

    // Авторизация
    std::string username;
    std::cout << "Please enter your username: ";
    std::getline(std::cin, username);

    if (username.empty() || username.length() > 63) {
        std::cerr << "Invalid username! It must be between 1 and 63 characters." << std::endl;
        return 1;
    }

    // Отправляем запрос авторизации
    sharedMem->message.type = Message::LOGIN;
    strncpy(sharedMem->message.username, username.c_str(), sizeof(sharedMem->message.username) - 1);
    sharedMem->message.username[sizeof(sharedMem->message.username) - 1] = '\0';
    strcpy(sharedMem->message.data, "Login request");

    // Уведомляем сервер
    sem_post(semClientReady);

    // Ждем ответа от сервера
    sem_wait(semServerReady);

    // Проверяем ответ на авторизацию
    if (sharedMem->message.type == Message::LOGIN_RESPONSE) {
        if (strcmp(sharedMem->message.data, "Already online") == 0) {
              std::cout << "Player is already online" << std::endl;
              exit(0);
        }
        std::cout << sharedMem->message.data << std::endl;
    } else {
        std::cerr << "Unexpected server response during login!" << std::endl;
        munmap(sharedMem, MMF_SIZE);
        close(fd);
        sem_close(semClientReady);
        sem_close(semServerReady);
        return 1;
    }

    // Основной игровой цикл
    std::string input;
    bool running = true;

    while (running) {
        std::cout << "\nOptions:\n";
        std::cout << "1. Create a new game\n";
        std::cout << "2. Join an existing game\n";
        std::cout << "3. View your statistics\n";
        std::cout << "4. Exit\n";
        std::cout << "Enter your choice (1-4): ";

        std::getline(std::cin, input);

        if (input == "1") {
            // Создание новой игры
            std::cout << "Enter game name: ";
            std::string gameName;
            std::getline(std::cin, gameName);

            if (gameName.empty() || gameName.length() > 63) {
                std::cout << "Invalid game name! It must be between 1 and 63 characters." << std::endl;
                continue;
            }

            // Отправляем запрос на создание игры
            sharedMem->message.type = Message::CREATE_GAME;
            strncpy(sharedMem->message.data, gameName.c_str(), sizeof(sharedMem->message.data) - 1);
            sharedMem->message.data[sizeof(sharedMem->message.data) - 1] = '\0';
            strcpy(sharedMem->message.username, username.c_str());

            // Уведомляем сервер
            sem_post(semClientReady);

            // Ждем ответа от сервера
            sem_wait(semServerReady);

            if (sharedMem->message.type == Message::CREATE_GAME_RESPONSE) {
                system("clear");
                std::cout << "Server response: " << sharedMem->message.data << std::endl;

                if (sharedMem->message.gameState == WAITING_FOR_PLAYER) {
                    if (strcmp(sharedMem->message.data, "Game with this name already exists!") == 0) {
                        continue;
                    }
                    if (strcmp(sharedMem->message.data, "Maximum number of games reached!") == 0) {
                        continue;
                    }
                    std::string gameName = sharedMem->message.gameName;
                    std::cout << "Waiting for an opponent to join..." << std::endl;

                    // Ждем пока оппонент присоединится
                    int pollCount = 0;
                    const int MAX_POLLS = 600; // 10 minutes maximum wait time at 1 second intervals
                    bool opponentJoined = false;

                    while (pollCount < MAX_POLLS && !opponentJoined) {
                        // Чекаем статус игры
                        sharedMem->message.type = Message::GAME_STATUS;
                        strcpy(sharedMem->message.username, username.c_str());
                        strcpy(sharedMem->message.gameName, gameName.c_str());

                        sem_post(semClientReady);
                        sem_wait(semServerReady);

                        if (sharedMem->message.type == Message::GAME_STATUS) {
                            // Оппонент подсоединился? - ставим корабли
                            if (sharedMem->message.gameState == PLACING_SHIPS) {
                                opponentJoined = true;
                                std::cout << "\nAn opponent has joined! Moving to ship placement phase..." << std::endl;

                                // Подсоединяемся к игре, чтобы начать ставить корабли
                                sharedMem->message.type = Message::JOIN_GAME;
                                strcpy(sharedMem->message.username, username.c_str());
                                strcpy(sharedMem->message.gameName, gameName.c_str());

                                sem_post(semClientReady);
                                sem_wait(semServerReady);

                                if (sharedMem->message.type == Message::JOIN_GAME_RESPONSE) {
                                    std::string opponentName = sharedMem->message.opponent;

                                    // Ставим корабли
                                    placeShips(sharedMem, semClientReady, semServerReady, username, gameName);

                                    // Ждем пока оппонент поставит корабли
                                    if (waitForOpponentShips(sharedMem, semClientReady, semServerReady, username, gameName)) {
                                        // Оба поставили - начинаем битву
                                        playGame(sharedMem, semClientReady, semServerReady, username, gameName,
                                                sharedMem->message.gameState, opponentName);
                                    }
                                }
                            }
                        }

                        // Снова мини анимашка
                        if (pollCount % 5 == 0) {
                            std::cout << "." << std::flush;
                        }

                        sleep(1); // Ждем секунду
                        pollCount++;
                    }

                    if (!opponentJoined) {
                        std::cout << "\nWaited too long for an opponent. Returning to main menu." << std::endl;
                    }
                }
            } else {
                std::cerr << "Unexpected server response!" << std::endl;
            }
        }
        else if (input == "2") {
            // Получаем список игр
            std::string gamesList = getGamesList(sharedMem, semClientReady, semServerReady, username);
            std::cout << "\n" << gamesList << std::endl;

            std::cout << "Enter game name to join (or 'back' to return): ";
            std::string gameName;
            std::getline(std::cin, gameName);

            if (gameName == "back") {
                continue;
            }

            if (gameName.empty()) {
                std::cout << "Game name cannot be empty!" << std::endl;
                continue;
            }

            // Запрос на подсоединение
            sharedMem->message.type = Message::JOIN_GAME;
            strcpy(sharedMem->message.username, username.c_str());
            strcpy(sharedMem->message.gameName, gameName.c_str());

            sem_post(semClientReady);
            sem_wait(semServerReady);

            if (sharedMem->message.type == Message::JOIN_GAME_RESPONSE) {
                std::cout << sharedMem->message.data << std::endl;
                std::string opponentName = sharedMem->message.opponent;

                if (sharedMem->message.gameState == PLACING_SHIPS) {
                    // Ставим корабли
                    placeShips(sharedMem, semClientReady, semServerReady, username, gameName);

                    // Игра готова или ждем оппонентов?
                    if (waitForOpponentShips(sharedMem, semClientReady, semServerReady, username, gameName)) {
                        // Корабли поставлены - начинаем!
                        playGame(sharedMem, semClientReady, semServerReady, username, gameName,
                                 sharedMem->message.gameState, opponentName);
                    }
                }
            } else {
                std::cerr << "Unexpected server response!" << std::endl;
            }
        }  else if (input == "3") {
            // Просмотр статистики
            viewStats(sharedMem, semClientReady, semServerReady, username);

        } else if (input == "4") {
            std::cout << "Thank you for playing. Goodbye!" << std::endl;
            running = false;

        } else {
            std::cout << "Invalid option. Please try again." << std::endl;
        }
    }

    // Освобождаем ресурсы
    sem_close(semClientReady);
    sem_close(semServerReady);
    munmap(sharedMem, MMF_SIZE);
    close(fd);

    return 0;
}

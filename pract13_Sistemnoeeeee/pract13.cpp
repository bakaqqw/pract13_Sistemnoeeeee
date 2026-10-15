#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>

std::string namePlayer[] = {
    "Боб", "Алекс", "Стив", "Альфа", "Бета",
    "Волк", "Лис", "Зебра", "Борис", "Геральд"
};

struct Player {
    long long health = 500000;
    int damage = 12000;
    int specialDamage = 30000;
    int attackCooldown = 2;
    int specialCooldown = 5;
    int defense = 20;
    int dodgeChance = 15;
    std::string name;
    long long totalDamage = 0;
};

struct Bayum {
    long long health = 9000000000LL;
    int resist = 44;
    int damage = 73843;
    int specialDamage = 150000;
    int attackCooldown = 5;
    int specialCooldown = 10;
};

const int COUNTS = 10;
int playerCount = 0;
bool game = true;
bool error = false;
int specialNumber = 0;
ULONGLONG startTime = 0;

Bayum boss;
Player players[COUNTS];
int playerId[COUNTS];
HANDLE playersThread[COUNTS] = {};
HANDLE bossThread = NULL;
HANDLE startEvent = NULL;
HANDLE bossEvent = NULL;
HANDLE gameOverEvent = NULL;
HANDLE playerEvent[COUNTS] = {};
CRITICAL_SECTION cs;

// Эти функции вызываются только внутри критической секции.
int AlivePlayers(int alive[]) {
    int count = 0;
    for (int i = 0; i < playerCount; i++) {
        if (players[i].health > 0) {
            alive[count++] = i;
        }
    }
    return count;
}

void CheckGame() {
    int alive[COUNTS];
    if (boss.health == 0 || AlivePlayers(alive) == 0) {
        game = false;
        SetEvent(gameOverEvent);
    }
}

void AttackBoss(int id, int damage, bool special) {
    long long realDamage = 1LL * damage * (100 - boss.resist) / 100;
    realDamage = std::min(realDamage, boss.health);
    boss.health -= realDamage;
    players[id].totalDamage += realDamage;
    std::cout << players[id].name
        << (special ? " использует спецприём: -" : " атакует: -")
        << realDamage << ", хп босса: " << boss.health << std::endl;
    CheckGame();
}

void AttackPlayer(int id, int damage) {
    if (rand() % 100 < players[id].dodgeChance) {
        std::cout << players[id].name << " увернулся" << std::endl;
        return;
    }
    long long realDamage = 1LL * damage * (100 - players[id].defense) / 100;
    realDamage = std::min(realDamage, players[id].health);
    players[id].health -= realDamage;
    std::cout << players[id].name << " -" << realDamage
        << " хп, осталось: " << players[id].health << std::endl;
    if (players[id].health == 0) {
        std::cout << players[id].name << " погиб" << std::endl;
        SetEvent(playerEvent[id]);
    }
}

// Ждём ближайшую атаку, не сбрасывая её время при пробуждении от события.
DWORD TimeToAttack(ULONGLONG attack, ULONGLONG special) {
    ULONGLONG next = std::min(attack, special);
    ULONGLONG now = GetTickCount64();
    return next > now ? static_cast<DWORD>(next - now) : 0;
}

void WaitError() {
    EnterCriticalSection(&cs);
    std::cout << "Ошибка ожидания: " << GetLastError() << std::endl;
    error = true;
    game = false;
    SetEvent(gameOverEvent);
    LeaveCriticalSection(&cs);
}

DWORD WINAPI FightPlayer(LPVOID param) {
    int id = *static_cast<int*>(param);
    if (WaitForSingleObject(startEvent, INFINITE) != WAIT_OBJECT_0) {
        WaitError();
        return 0;
    }
    ULONGLONG nextAttack = startTime + players[id].attackCooldown * 1000ULL;
    ULONGLONG nextSpecial = startTime + players[id].specialCooldown * 1000ULL;
    int lastSpecial = 0;
    HANDLE events[] = { gameOverEvent, bossEvent, playerEvent[id] };

    while (true) {
        DWORD result = WaitForMultipleObjects(3, events, FALSE,
            TimeToAttack(nextAttack, nextSpecial));
        if (result == WAIT_FAILED) {
            WaitError();
            return 0;
        }

        EnterCriticalSection(&cs);
        if (!game || players[id].health == 0) {
            LeaveCriticalSection(&cs);
            break;
        }

        if (lastSpecial != specialNumber) {
            lastSpecial = specialNumber;
            std::cout << players[id].name << " проснулся после спецатаки босса" << std::endl;
        }

        ULONGLONG now = GetTickCount64();
        if (now >= nextAttack) {
            AttackBoss(id, players[id].damage, false);
            nextAttack = now + players[id].attackCooldown * 1000ULL;
        }
        if (game && now >= nextSpecial) {
            AttackBoss(id, players[id].specialDamage, true);
            nextSpecial = now + players[id].specialCooldown * 1000ULL;
        }
        LeaveCriticalSection(&cs);
    }
    return 0;
}

DWORD WINAPI FightBoss(LPVOID) {
    if (WaitForSingleObject(startEvent, INFINITE) != WAIT_OBJECT_0) {
        WaitError();
        return 0;
    }
    ULONGLONG nextAttack = startTime + boss.attackCooldown * 1000ULL;
    ULONGLONG nextSpecial = startTime + boss.specialCooldown * 1000ULL;

    while (true) {
        DWORD result = WaitForSingleObject(gameOverEvent,
            TimeToAttack(nextAttack, nextSpecial));
        if (result == WAIT_FAILED) {
            WaitError();
            return 0;
        }
        if (result == WAIT_OBJECT_0) break;

        EnterCriticalSection(&cs);
        if (!game) {
            LeaveCriticalSection(&cs);
            break;
        }
        ULONGLONG now = GetTickCount64();
        int alive[COUNTS];
        int count = AlivePlayers(alive);

        if (now >= nextAttack && count > 0) {
            int target = alive[rand() % count];
            std::cout << "Босс атакует " << players[target].name << std::endl;
            AttackPlayer(target, boss.damage);
            nextAttack = now + boss.attackCooldown * 1000ULL;
            CheckGame();
        }

        if (game && now >= nextSpecial) {
            count = AlivePlayers(alive);
            int damage = boss.specialDamage * (100 - 5 * (count - 1)) / 100;
            std::cout << "Босс: спецатака по " << count
                << " игрокам, урон до защиты: " << damage << std::endl;
            for (int i = 0; i < count; i++) {
                AttackPlayer(alive[i], damage);
            }
            specialNumber++;

            // PulseEvent-будим всех ожидающих bossEvent
            if (!PulseEvent(bossEvent)) {
                std::cout << "Ошибка (P): " << GetLastError() << std::endl;
            }
            
            for (int i = 0; i < count; i++) {
                SetEvent(playerEvent[alive[i]]);
            }
            nextSpecial = now + boss.specialCooldown * 1000ULL;
            CheckGame();
        }
        LeaveCriticalSection(&cs);
    }
    return 0;
}

void Statistics() {
    std::cout << "\n" << (boss.health == 0 ? "Игроки победили" : "Босс победил")
        << std::endl;
    std::cout << "Здоровье босса: " << boss.health << std::endl;
    std::cout << "Топ-3 по нанесённому урону:" << std::endl;
    int order[COUNTS];
    for (int i = 0; i < playerCount; i++) order[i] = i;
    for (int i = 0; i < playerCount; i++) {
        for (int j = i + 1; j < playerCount; j++) {
            if (players[order[j]].totalDamage > players[order[i]].totalDamage) {
                std::swap(order[i], order[j]);
            }
        }
    }
    for (int i = 0; i < std::min(3, playerCount); i++) {
        int id = order[i];
        std::cout << i + 1 << ". " << players[id].name << " - "
            << players[id].totalDamage << " урона" << std::endl;
    }
}

void CloseResources() {
    if (bossThread) CloseHandle(bossThread);
    for (int i = 0; i < playerCount; i++) {
        if (playersThread[i]) CloseHandle(playersThread[i]);
        if (playerEvent[i]) CloseHandle(playerEvent[i]);
    }
    if (startEvent) CloseHandle(startEvent);
    if (bossEvent) CloseHandle(bossEvent);
    if (gameOverEvent) CloseHandle(gameOverEvent);
    DeleteCriticalSection(&cs);
}

bool start() {
    InitializeCriticalSection(&cs);
    startEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    bossEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    gameOverEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    bool created = startEvent && bossEvent && gameOverEvent;
    for (int i = 0; i < playerCount && created; i++) {
        playerEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        created = playerEvent[i] != NULL;
    }
    if (!created) {
        std::cout << "Ошибка создания события: " << GetLastError() << std::endl;
        CloseResources();
        return false;
    }

    for (int i = 0; i < playerCount; i++) {
        players[i].name = namePlayer[i];
        playerId[i] = i;
        std::cout << "Игрок " << i + 1 << ": " << players[i].name << std::endl;
    }

    bossThread = CreateThread(NULL, 0, FightBoss, NULL, 0, NULL);
    created = bossThread != NULL;
    for (int i = 0; i < playerCount && created; i++) {
        playersThread[i] = CreateThread(NULL, 0, FightPlayer, &playerId[i], 0, NULL);
        created = playersThread[i] != NULL;
    }
    if (!created) {
        std::cout << "Ошибка создания потока: " << GetLastError() << std::endl;
        EnterCriticalSection(&cs);
        game = false;
        error = true;
        SetEvent(gameOverEvent);
        LeaveCriticalSection(&cs);
    }

    startTime = GetTickCount64();
    if (created) {
        std::cout << "Бой начался. Босс: " << boss.health << " хп" << std::endl;
    }
    SetEvent(startEvent);

    // Ждём завершения всех созданных потоков, затем читаем статистику.
    if (bossThread) WaitForSingleObject(bossThread, INFINITE);
    for (int i = 0; i < playerCount; i++) {
        if (playersThread[i]) WaitForSingleObject(playersThread[i], INFINITE);
    }
    if (!error) Statistics();
    CloseResources();
    return !error;
}

int main() {
  
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    srand(static_cast<unsigned int>(time(NULL)));
    std::cout << "Введите количество игроков от 1 до 10: ";
    std::string input;
    std::getline(std::cin, input);
    if (input == "10") playerCount = 10;
    else if (input.size() == 1 && input[0] >= '1' && input[0] <= '9') {
        playerCount = input[0] - '0';
    }
    else {
        std::cout << "Нужно ввести целое число от 1 до 10" << std::endl;
        return 1;
    }
    return start() ? 0 : 1;
}

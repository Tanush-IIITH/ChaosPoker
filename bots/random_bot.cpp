#include <iostream>
#include <sstream>
#include <string>
#include <random>

int main() {
    std::mt19937 rng(std::random_device{}());

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "ACTION_PROMPT") {
            int chips, current_bet, my_bet, min_raise, pot;
            iss >> chips >> current_bet >> my_bet >> min_raise >> pot;

            int to_call = current_bet - my_bet;

            // 20% fold, 40% call/check, 30% raise, 10% all-in
            int roll = std::uniform_int_distribution<int>(1, 100)(rng);

            if (roll <= 10) {
                std::cout << "ALLIN" << std::endl;
            } else if (roll <= 40) {
                // raise to a random amount between min_raise and all-in
                int max_raise = chips + my_bet;
                if (min_raise <= max_raise && min_raise > current_bet) {
                    int amount = std::uniform_int_distribution<int>(min_raise, max_raise)(rng);
                    std::cout << "RAISE " << amount << std::endl;
                } else if (to_call <= 0) {
                    std::cout << "CHECK" << std::endl;
                } else if (to_call <= chips) {
                    std::cout << "CALL" << std::endl;
                } else {
                    std::cout << "ALLIN" << std::endl;
                }
            } else if (roll <= 80) {
                if (to_call <= 0) {
                    std::cout << "CHECK" << std::endl;
                } else if (to_call <= chips) {
                    std::cout << "CALL" << std::endl;
                } else {
                    std::cout << "ALLIN" << std::endl;
                }
            } else {
                if (to_call <= 0) {
                    std::cout << "CHECK" << std::endl;
                } else {
                    std::cout << "FOLD" << std::endl;
                }
            }
        } else if (cmd == "SWAP_PROMPT") {
            int cost, chips;
            iss >> cost >> chips;

            // 50% stay, 25% swap card 0, 25% swap card 1
            if (cost > chips) {
                std::cout << "STAY" << std::endl;
            } else {
                int roll = std::uniform_int_distribution<int>(1, 100)(rng);
                if (roll <= 50) {
                    std::cout << "STAY" << std::endl;
                } else if (roll <= 75) {
                    std::cout << "SWAP 0" << std::endl;
                } else {
                    std::cout << "SWAP 1" << std::endl;
                }
            }
        } else if (cmd == "VOTE_PROMPT") {
            int chips;
            iss >> chips;

            // random YES/NO with a random wager (0 to 10% of chips)
            int max_wager = std::max(1, chips / 10);
            int wager = std::uniform_int_distribution<int>(0, max_wager)(rng);

            if (std::uniform_int_distribution<int>(0, 1)(rng) == 0) {
                std::cout << "VOTE YES " << wager << std::endl;
            } else {
                std::cout << "VOTE NO " << wager << std::endl;
            }
        }
    }
    return 0;
}

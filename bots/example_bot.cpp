#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "ACTION_PROMPT") {
            int chips, current_bet, my_bet, min_raise, pot;
            iss >> chips >> current_bet >> my_bet >> min_raise >> pot;

            int to_call = current_bet - my_bet;
            if (to_call <= 0) {
                std::cout << "CHECK" << std::endl;
            } else if (to_call <= chips) {
                std::cout << "CALL" << std::endl;
            } else {
                std::cout << "ALLIN" << std::endl;
            }
        } else if (cmd == "SWAP_PROMPT") {
            std::cout << "STAY" << std::endl;
        } else if (cmd == "VOTE_PROMPT") {
            std::cout << "VOTE YES 0" << std::endl;
        }
        // all other messages are informational — ignore them
    }
    return 0;
}

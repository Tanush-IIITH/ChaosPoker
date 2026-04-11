// Bot that always sends garbage — used to test error-split rule
#include <iostream>
#include <string>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        // respond with garbage to every prompt
        if (line.find("PROMPT") != std::string::npos) {
            std::cout << "GARBAGE" << std::endl;
        }
    }
    return 0;
}

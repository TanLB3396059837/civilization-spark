#include "civ/civilization/simulation.hpp"

#include <iostream>

int main() {
    std::cout << "[simulation lifetime test]" << std::endl;
    {
        civ::Simulation simulation(7);
        std::cout << "constructed" << std::endl;
        simulation.shutdown();
        std::cout << "shutdown" << std::endl;
    }
    std::cout << "destroyed" << std::endl;
    civ::SharedModelPool::shutdown_active();
    return 0;
}

#include "Application.h"
#include <exception>
#include <iostream>

/* Native executable entry point; no original address. Report uncaught host
 * failures and return a nonzero status to launchers and automated checks. */
int main(int argc, char **argv) {
    try {
        return runApplication(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

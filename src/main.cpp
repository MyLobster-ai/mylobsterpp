#include "openclaw/cli/app.hpp"

int main(int argc, char** argv) {
    openclaw::cli::App app;
    return app.run(argc, argv);
}

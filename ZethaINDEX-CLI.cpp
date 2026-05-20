#include "ZethaINDEX.hpp"

int main(int argc, char** argv) {
    return zetha::index::run_cli(zetha::index::args_to_vector(argc, argv));
}

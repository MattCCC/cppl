#include "cppl/driver/crash.hpp"
#include "cppl/driver/driver.hpp"

int main(int argc, char** argv) {
    cppl::driver::install_crash_report();
    return cppl::driver::run_driver(argc, argv);
}

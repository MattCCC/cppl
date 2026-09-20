#pragma once

namespace cppl::driver {

// Installs a report for the crashes the operating system delivers as a signal
// or a structured exception. The report names the stage the compiler was in,
// so a crash under automation says more than an exit status.
void install_crash_report();

// The stage the compiler is in. The enclosing stage is restored at the end, so
// these nest. `name` and `input` must outlive the object: both are read by the
// crash report without copying, which is all a handler may do.
class Stage {
  public:
    explicit Stage(const char* name, const char* input = nullptr) noexcept;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    ~Stage() noexcept;

  private:
    const char* previous_name_;
    const char* previous_input_;
};

} // namespace cppl::driver

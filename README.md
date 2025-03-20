# cpp-libs
My header-only cpp libraries

These headers are C++17 compliant handly libraries written by me. If you want to use those, just copy the header file, include it and you're good to go.

## `hwstat.h`

Low-overhead, multi-thread friendly performance counters. Support user-defined stats and pretty-printing.

<details>
<summary>Usage</summary>

```cpp
// define a counter
COUNTER(fooCounter)
COUNTER(barCounter, "description for bar counter")

// define a timer
TIMER(fooTimer)
TIMER(barTimer, "description for bar timer")

// custom user stats takes a function(typically lambda) so that you can include your own stats.
STAT(myRate, []() {
 auto num = fooTimer.stat().cycles;
 auto den = num + BarTimer.stat().cycles;
 return std::to_string(double(num) / den);
})

void doTimeConsumingStuff() {
  std::this_thread_sleep_for(std::chrono_milliseconds(10));
}

void doMisc() {
  /* do nothing */
}

int main(void) {
  // updating counters
  fooCounter++;
  barCounter += 2;

  // time a code block with ScopedTimer
  {
    hwstat::ScopedTimer st(fooTimer);
    doTimeConsumingStuff();
  }

  // it's thread safe
  std::thread t([]() {
    hwstat::ScopedTimer st(fooTimer);
    doTimeConsumingStuff();
  });

  // or use a stopwatch
  Stopwatch sw(fooTimer);
  doTimeConsumingStuff();
  sw.pause();
  doMisc();
  sw.resume();
  doTimeConsumingStuff();
  sw.stop();

  t.join();

  // print stats
  hwstat::print_counter_stats();
  hwstat::print_timer_stats();
  hwstat::print_user_stats();
  // or use hwstat::print_stats() to print all stats

  return 0;
}
```
</details>

## `args.h`

A command line argument parser. Support all primitives and enums, fully declarative & constexpr.

## `thread_pool.h`

A thread pool implementation.

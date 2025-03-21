# cpp-libs
My header-only cpp libraries

These headers are C++17 compliant handy libraries written by me. If you want to use those, just copy the header file, include it and you're good to go.

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
 auto den = num + barTimer.stat().cycles;
 return std::to_string(double(num) / den);
})

void doTimeConsumingStuff() {
  std::this_thread::sleep_for(std::chrono_milliseconds(10));
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

A command line argument parser. Support all primitives, strings & enums, fully declarative & constexpr.

<details>
<summary>Usage</summary>

```cpp

enum class TestType {
  Seq,
  Rnd,
};

struct Config {
  TestType test_type;
  unsigned thread_cnt;
  size_t blk_sz;
  size_t mem_sz;
  int test_cnt;
} conf;

static arg::Parser parser{
    arg::EnumArg<TestType>('t', conf.test_type, "type", TestType::Seq,
                           "type of test"),
    arg::UIntArg('j', conf.thread_cnt, "thread-cnt", 1, "number of threads"),
    arg::SizeArg('s', conf.mem_sz, "memory-size", 1UL * GB,
                 "total memory size"),
    arg::SizeArg('b', conf.blk_sz, "block-size", 256UL, "block size, 64B ~ 16K, power of 2"),
    arg::IntArg('c', conf.test_cnt, "test-count", 50,
                "total number of cycles to run"),
};

int main(int argc, const char *argv[]) {
  try {
    parser.parse(argc, argv);
    parser.printAll(std::cout);
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    parser.usage(std::cerr, argv[0]);
    exit(EXIT_FAILURE);
  }

  // ...run your program
  return 0;
}

```

Example output:
```bash
$ ./example -h
Usage: ./example
  -t(--type) [default = Seq]
        type of test
  -j(--thread-cnt) [default = 1]
        number of threads
  -s(--memory-size) [default = 1GB]
        total memory size
  -b(--block-size) [default = 256B]
        block size, 64B ~ 16K, power of 2
  -c(--test-count) [default = 50]
        total number of cycles to run
  -h(--help)
        print this help message
```
```bash
$ ./example -t rnd -c 10 -b 2K
values:
  -t(--type)    Rnd
  -j(--thread-cnt)      4
  -s(--memory-size)     1GB
  -b(--block-size)      2KB
  -c(--test-count)      10
```
</details>

## `thread_pool.h`

A thread pool implementation.

# pam2qoi – A C++17 parallel Quite OK Image encoder

`pam2qoi` encodes a [Portable Arbitrary Map](https://en.wikipedia.org/wiki/Netpbm#PAM_graphics_format) provided on `STDIN` to a [Quite OK Image](https://qoiformat.org/) on `STDOUT` while benchmarking read and write times on `STDERR`. It is written in pure C++ and has no library dependencies.

## Motivation

QOI is an interesting format as it is quite easy to integrate into projects with the need for quick (and not so dirty) image output. I had to have such output for debugging purposes in a project and needed a simple format with alpha channel and modest compression.

After more than "quite OK" results I did this standalone implementation for fun and learning. Fun was making up a scheme to parallelize the encoding step, and learning about [`std::async()`](https://en.cppreference.com/w/cpp/thread/async) and the speed of `std::ostream`.

## Implementation

First of all there is a simple move-only `Image` class holding RGBA pixels. An instance of this class is created in `readPam()`, moved to `main()` upon return, and then passed as a const reference to `encodeQoi()`.

Originally, `readPam()` took byte by byte from the input stream, but it is much faster to fetch a whole line at once and construct the pixels from that line. `readPam()` was able to read double byte color components (`MAXVAL > 255`) by skipping the LSB in the slow implementation. Now it only accepts PAMs with 8 bits per component and either three (RGB) or four (RGBA) components per pixel.

For `encodeQoi()` I tried different output strategies:

1. Using `std::ostream&` as an output variable. This consumes the least memory but was also the slowest.
2. Like 1. but writing all data to a preallocated `std::string` inside `encodeQoi()` which was streamed into the `std::ostream&` at the end of the function. This was way faster.
3. Using `std::string&` as an output variable, preallocated inside `encodeQoi()`. Surprisingly, that was slower than 2.
4. Returning a `std::string`. This is the fastest solution and currently implemented.

`start_y` and `end_y` allow for dividing the image into separately encodable stripes. There's a bit of extra code so that the QOI header is only encoded for the first stripe and that the end marker only comes at the end of the last. Also the `index` array is only filled for the first stripe. Else it cannot assume anything, so all places in the index are invalid, which is ensured by the `std::optional<>`. The rest is closely modeled after the reference code.

Encoding the QOI in `main()` has two cases: the single-threaded and the multi-threaded one. We don't need to talk about the single-threaded one-liner. In the multi-threaded branch the image is split into `lines_per_pack` for every thread. Only the first thread, which has the advantage to start earlier than its successors, gets some lines more so that the division is integer. The rest is uncharitable benchmark code.

## Compilation

I found Clang to produce faster code for writing the QOI, while `readPam()` was faster with GCC.

```shell
$ clang++ -std=c++17 -O3 -o pam2qoi pam2qoi.cpp
```

Or

```shell
$ g++ -std=c++17 -O3 -o pam2qoi pam2qoi.cpp
```

If your compiler is new enough you can omit the `-std=c++17`.

## Usage

If no argument is provided `pam2qoi` will use all available threads. Sometimes this is not desirable, so you can give the number of threads as the only argument to the program. Anyway, it can't be higher than the number of available threads and is silently reduced to that number. I didn't spend much time on pretty error handling, so giving a name instead of a number will result in a terse error description.

```shell
$ ./pam2qoi < 56Mpix.pam > 56Mpix.qoi
Read: 263ms
Write: 98ms
$ ./pam2qoi 1 < 56Mpix.pam > 56Mpix.qoi
Read: 264ms
Write: 703ms
```

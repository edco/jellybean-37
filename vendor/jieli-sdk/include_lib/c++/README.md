# C++ 代码使用说明

C++ 标准库有一些特性会占用较多的空间。一些情况下，我们可能并不会使用，为了兼顾程序的体积，C++ 标准库编译了各种不同的配置。

长跳转是指编译时，添加了 `-mllvm -pi32v2-large-program=true`。这个使得可以`call`更远距离的函数（同时生成的库函数体积也会更大一些）。

## C++ 库列表说明

| 长跳转    | 线程 (thread) | 异常（exception）| locale | 名称 |
|:--------:|:-------------:|:--------------:|:------:|:---:|
|   未支持  |  未支持        | 未支持          | 未支持   | median |
|   未支持  |  未支持        | 未支持          |  支持    | locale-median |
|   未支持  |  支持         | 未支持           | 未支持   | pthread-median |
|   未支持  |  支持         | 未支持           |  支持    | pthread-locale-median |
|   未支持  |  未支持        | 支持            | 未支持   | exception-median |
|   未支持  |  未支持        | 支持            |  支持    | exception-locale-median |
|   未支持  |  支持         | 支持            | 未支持   | exception-pthread-median |
|   未支持  |  支持         | 支持            |  支持    | exception-pthread-locale-median |
|   支持  |  未支持        | 未支持           | 未支持   | large |
|   支持  |  未支持        | 未支持           |  支持    | locale-large |
|   支持  |  支持         | 未支持            | 未支持   | pthread-large |
|   支持  |  支持         | 未支持            |  支持    | pthread-locale-large |
|   支持  |  未支持        | 支持             | 未支持   | exception-large |
|   支持  |  未支持        | 支持             |  支持    | exception-locale-large |
|   支持  |  支持         | 支持              | 未支持   | exception-pthread-large |
|   支持  |  支持         | 支持              |  支持    | exception-pthread-locale-large |

## 集成的步骤

为了集成 C++ 标准库，主要需要做下面几个步骤：

1. 确定需要使用的 C++ 特性，选择对应的 C++ 标准库
2. 编译参数添加标准库的搜索路径，例如 `-I ${CXXDIR}/large/include/c++/v1`。注意需要将其添加到 C 标准库的搜索路径之前，即 `-I /opt/jieli/pi32v2/include` 之前。
3. 链接参数中，需要添加 `libc++.a`, `libc++abi.a` 的路径。
    - 如果使用了异常，还需要增加 `libunwind.a`。且链接参数增加 `--eh-frame-hdr`
    - 如果使用了 TLS 变量，还需要增加 `libemutls.a`（且需要依赖 `pthread`）
4. 链接参数建议加上 `--gc-sections` 以缩减体积。
5. 处理全局构造函数，参考下面注意事项。
6. 一些函数可能在 C 库中确实（未提供，例如 `_malloc_r`, `sscanf` 等）。需要在 SDK 中补充实现。

## 注意事项

1. 我们使用的是 clang 4.0.1，可以支持到 c++14 以及部分的 c++17
    分别是：
    - `-std=c++11`, `-std=gnu++11` # c++11
    - `-std=c++14`, `-std=gnu++14` # c++14
    - `-std=c++1z`, `-std=gnu++1z` # c++17 (部分)

    建议都使用 gnu++1n 的写法。这样不会定义 `__STRICT_ANSI__` 宏，避免问题（如果定义了`__STRICT_ANSI__`，一些函数会缺少声明）。

2. clang 编译 `.c` 后缀的时候，是 C 编译器。
    编译 `.cxx`, `.cc`, `.cpp` 文件的时候，是当做 C++ 模式处理。
    所以注意当前文件后缀是否正确。

3. C++ 标准库使用的版本是 llvm-12.0.1:

    - 不支持的部分：
        `filesystem`
        `std::cin`, `std::cout`
        `fstream` 相关操作

    - 支持的部分：
        STL(`vector`, `list`, `map`, `functional`, `algorithm` 等)
        `thread`, `mutex`, `future` 等 (依赖于 `pthread` 接口实现)
        `strstream` 等（依赖于 `locale`）

4. 如果要使用 TLS 变量，即 `thread_local` 以及 `__thread` 等变量。
    则需要编译参数加上 `-femulated-tls`，链接参数加上 `--plugin-opt=-emulated-tls`。
    链接库需要加上 `libemutls.a`，且依赖 `pthread` 接口。

5. 可以支持异常，异常使用的是基于 dwarf (`.eh_frame`)的处理方式，也就是编译器静态生成栈帧回退的数据结构，在抛出异常时候才会使用。

    链接脚本需要增加下面的部分：

    ```ld
    .text : { 
        *(.text*)
        *(.gcc_except_table*);
    }

    .eh_frame : {
        __eh_frame_start = .;
        KEEP(*(.eh_frame)); // 注意要有 KEEP
        __eh_frame_end = .;
    }

    .eh_frame_hdr : {
        __eh_frame_hdr_start = .;
        KEEP(*(.eh_frame_hdr)); // 注意要有 KEEP
        __eh_frame_hdr_end = .;
    }

    ```

    在运行时，需要把 `.eh_frame`, `.eh_frame_hdr` 放到 flash 中（也可内存，这两部分数据是只读的）

    链接参数还需要加上 `--eh-frame-hdr` 这样才会生成 `.eh_frame_hdr` 段。用以异常处理。
    编译 C 代码时，需要加上 `-funwind-tables`，避免在 C 代码部分异常未能正确回滚栈帧。

    需要额外链接 libunwind.a。

    会增加较多的额外代码以及数据量，固件体积会增加较多。一般情况下，不推荐使用异常。

6. 一般情况下，推荐关闭异常，以及 RTTI：`-fno-exceptions`, `-fno-rtti`

    如果使用了 `dynamic_cast`，则需要保留 RTTI，不能使用 `-fno-rtti`

7. 头文件引用顺序
    1. 注意，先搜索 C++ 头文件，再搜索 C 的头文件。避免冲突。也就是，先 -I C++ 的标准库头文件路径，再 -I C 的标准库头文件路径
    2. 如果用了 `pthread`，则注意不能使用到 C 标准库里面的 `pthread.h`。建议直接弃用系统路径的 C 标准库（拷贝一份到 SDK，并删除其中的 `pthread.h`）
    3. `pthread` 需要使用 `simple_pthread` 文件夹中提供的部分。这个里面的 `pthread.h` 和 `semaphore.h` 是手动将 OS 相关的内容提取出来，生成的头文件，保证了较少的外部依赖（`clang -E -p` 参数进行宏展开后，再手动拷贝相关函数声明以及结构体定义）

8. 当需要使用 `iostream`, `strstream` 等相关的内容时候，需要依赖于 `locale` 相关的 api
    如果有报错 `error "Localization is not supported by this configuration of libc++"` 则说明需要换一个 libc++ 的库

9. 处理全局变量的构造和析构。

    链接脚本添加：

    ```ld
    // 全局变量的构造函数列表
    ___ctors_begin = .;
    KEEP(*(SORT(.ctors.*))) // 注意要有 KEEP
    KEEP(*(.ctors))         // 注意要有 KEEP
    ___ctors_end = .;
    ```

    在 main 函数调用前，调用全局变量的构造函数：

    ```c
      typedef void (*pfunc) ();
      extern pfunc ___ctors_begin[];
      extern pfunc ___ctors_end[];
      pfunc *p;

      // 调用全局构造函数（注意是要逆序）
      for (p = ___ctors_end; p > ___ctors_begin; )
        (*--p) (); // 注意这里是倒序调用的
    ```

    需要添加 `__cxa_atexit`, `__cxa_finalize` 函数：

    ```c
    // https://refspecs.linuxbase.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/baselib---cxa-atexit.html
    int __cxa_atexit(void (*func) (void *), void * arg, void * dso_handle);
    void __cxa_finalize(void *f);
    ```

    需要添加 `__dso_handle` 定义：

    ```c
    // 这个在调用 __cxa_atexit 的时候需要用到
    void *__dso_handle = &__dso_handle;
    ```

10. `new`, `delete`, `__cxa_pure_virtual`, `__cxa_deleted_virtual` 这些函数在 `libc++abi.a` 中已经定义

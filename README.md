## Chip

Chip is an implementation of coroutines and a user-space scheduler.

### Example

TODO

### Design

Fundamentally, a coroutine library is two things: a user-space scheduler and a stack allocator.

#### Scheduling

For the most part, tasks are FIFO scheduled. When asynchronous I/O is involved, tasks are scheduled in the order in which epoll/kqueue returns them. Additionally, no polling-related system calls are made until the runnable queue has been exhausted, which helps to amortize the cost of talking to the operating system.

In order to help manage memory consumption, the scheduler maintains a separate queue of tasks that wish to allocate new tasks. (You park on this queue when you call `spawn()`.) When a task exits, it first checks if it can 'gift' its stack to the highest-priority allocator (see `task_handoff()`), which saves the cost of free-ing the task and then re-allocating it. Similarly, only when the run-queue is exhausted does the scheduler begin allocating new tasks to give to allocators. Thus, tasks are only allocated when the scheduler has proved that *not* allocating a new task would lead to deadlock.

#### Stack allocation

Stacks (and their associated metadata, see `task_t`) are arena-allocated using `mmap()`. Each arena contains `sizeof(uintptr_t)*8` tasks (because each arena is just a first-fit bitmap allocator). All but the most-recently-used empty arenas are soft-offlined by the memory manager (through `madvice(MADV_DONTEED)` or equivalent.) The arena selected for new allocation is just the arena from which the last task was free'd. This keeps all allocations O(1) and with reasonable locality. (Note that pure-LIFO stack allocation would have the best temporal locality for the first allocated stack, but then declining temporal locality for each stack subsequently allocated. Instead, we always allocate the lowest-addressed free stack from each arena, which has optimal spatial locality, and reasonably good temporal locality, because it is still LIFO in the one-stack case.)

##### Stack Guards

Since coroutine stacks have to be relatively small in order to support many (possibly millions) running on the same machine, the possibility of overflowing one of the stacks is very real. Consequently, things like large stack buffers and recursion are strongly discouraged. (Presently, coroutine stacks are mapped 12kB apart.)

In order to guard against stack overflow, the runtime inserts a canary at the top of every stack that is checked before it is scheduled. (The value of the canary is unique to each stack, so even for completely deterministic programs it will be randomized on platforms that implement [ASLR](https://en.wikipedia.org/wiki/Address_space_layout_randomization).) We use canaries instead of guard pages for two reasons: data locality and [VMA](http://www.makelinux.net/books/lkd2/ch14lev1sec2) conservation. If we were to insert a guard page below every stack, we would run the risk of exhausting kernel VMAs, or forcing large parts of user and kernel memory to be swapped, which would degrade application scalability. The drawback to this approach is that programs do not immediately fault if they clobber another task's stack; instead, we only find the corruption when the clobbered stack is scheduled. My recommendation is to compile your programs with `-fstack-usage` (on GCC) which will tell you the stack requirements of every function in your program. (Additionally, keep in mind that programs compiled with `-O3` and `-flto` will consume much less stack space than unoptimized programs; inlining is your friend!)

### Building

#### Supported Platforms

Chip supports Unix systems on x86_64 and ARMv7 (you need support for the `ldm` and `stm` instructions), although most of the optimization effort has gone into Linux+ARMv7+musl-gcc builds, since that is *my* deployment environment. Porting the runtime to new architectures/ABIs is pretty easy, but each new platform imposes new testing requirements. Today, I test changes on Linux/x86_64, Linux/ARMv7, and OSX/x86_64.

##### OS support

Presently, the runtime depends upon the following system calls being available:

 - Either `epoll` or `kqueue`
 - `mmap`
 - `madvise` with `MADV_DONTNEED` on Linux or `MADV_FREE` on BSDs. (Anonymous mappings are never unmapped; instead we just let the kernel reclaim the page table entries and let the pages get faulted back in as necessary.)

Notably, the runtime does *not* depend upon having access to a traditional memory allocator.

#### Compiler and libc

The code can be compiled with clang or gcc, with gcc as the default (or musl-gcc, if it is available.) I highly recommend statically linking against [musl libc](http://musl-libc.org), which is much more parsimonious with its stack consumption (particularly in the `{vfns}printf` family of functions), and thus less likely to overflow a tiny stack.

#### Build Script

In the source directory, you can use the `build` script to build, test, benchmark, and install the library.

(You'll need the [rc shell](http://github.com/rakitzis/rc) to run the build script.)

 - `./build test` builds and runs test binaries
 - `./build install` builds and installs the library and header files.
 - `./build bench` builds and runs benchmark binaries (and depends on `install`.)
 - `./build uninstall` un-does what `./build install` does.


### License

All the code in this repository is licensed under the Mozzilla Public License Version 2.0. 
For future references:

Essential Books

"The Art of Multiprocessor Programming" by Maurice Herlihy & Nir Shavit
https://cs.ipm.ac.ir/asoc2016/Resources/Theartofmulticore.pdf

"C++ Concurrency in Action" by Anthony Williams
https://www.manning.com/books/c-plus-plus-concurrency-in-action

"Lock-Free Data Structures with Hazard Pointers" by Andrei Alexandrescu
https://erdani.org/publications/cuj-2004-12.pdf

"Systems Programming: Coping with Parallelism" by R.K. Treiber (1986)
https://dominoweb.draco.res.ibm.com/reports/rj5118.pdf (might be outdated)

Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms" by Michael & Scott (1996)
https://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf

"Split-Ordered Lists: Lock-Free Extensible Hash Tables" by Ori Shalev & Nir Shavit (2006)
https://ldhulipala.github.io/readings/split_ordered_lists.pdf

"A Pragmatic Implementation of Non-Blocking Linked-Lists" by Timothy L. Harris (2001)
https://www.cl.cam.ac.uk/research/srg/netos/papers/2001-caslists.pdf

"A Simple Optimistic skip-list Algorithm" by Maurice Herlihy et al. (2007)
https://people.csail.mit.edu/shanir/publications/LazySkipList.pdf

"Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects" by Maged M. Michael (2004)
https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf

"Pass The Buck: A Distribute Epoch-Based Memory Reclamation" by Keir Fraser (2007)
https://www.cs.rochester.edu/u/scott/papers/2018_PPoPP_IBR.pdf

Practical Implementations & Libraries

Folly (Facebook's C++ Library) https://github.com/facebook/folly
https://github.com/facebook/folly

Intel TBB (Threading Building Blocks)
https://github.com/uxlfoundation/oneTBB

Boost.Lockfree
https://github.com/boostorg/lockfree

RCU (Read-Copy-Update) - Linux kernel technique
https://www.kernel.org/doc/html/next/RCU/whatisRCU.html

QSBR (Quiescent State-Based Reclamation)
https://sysweb.cs.toronto.edu/publication_files/0000/0159/jpdc07.pdf

Tagged Pointers - Add version numbers
Double-Compare-and-Swap (DCAS)
"Software Transactional Memory" by Nir Shavit & Dan Touitou (1995)

"Dynamic Circular Work-Stealing Deque" by David Chase & Yossi Lev (2005)
"Flat Combining and the Synchronization-Parallelism Tradeoff" by Hendler, Incze, Shavit & Tzafrir (2010)

Online Resources

Conferences
•  PODC (Principles of Distributed Computing)
•  SPAA (Symposium on Parallelism in Algorithms and Architectures)
•  PPoPP (Principles and Practice of Parallel Programming)

Websites
•  preshing.com - Excellent blog on concurrent programming
•  1024cores.net - Dmitry Vyukov's site (lots of lock-free algorithms)
•  mechanical-sympathy.blogspot.com - Martin Thompson's blog

https://www.youtube.com/watch?v=fvcbyCYdR10&ab_channel=JonGjengset

https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2530r3.pdf

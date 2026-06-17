#include "ring_queue.h"
#include <iostream>

int main() {
    spsc::RingQueue<int, 3> q;

    std::cout << "capacity = " << q.capacity() << ", empty = " << std::boolalpha
              << q.empty() << "\n";

    for (int i = 1; i <= 5; ++i) {
        bool ok = q.push(i);
        std::cout << "push(" << i << ") -> " << (ok ? "ok" : "FULL")
                  << "  size=" << q.size() << "\n";
    }

    int x;
    while (q.pop(x)) {
        std::cout << "pop() -> " << x << "  size=" << q.size() << "\n";
    }

    std::cout << "empty = " << q.empty() << "\n";
    return 0;
}
